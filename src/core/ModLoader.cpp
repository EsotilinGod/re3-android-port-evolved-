// ============================================================================
// Carpeta "mods" estilo ModLoader (Android).
//
// Todo lo que se ponga dentro de re3GTA/mods (en cualquier subcarpeta) se usa
// en lugar del archivo original del mismo nombre, sin tocar el juego:
//
//  * .dff / .txd que existen dentro de models/gta3.img (autos, peatones,
//    edificios, texturas...): el streaming los lee desde la carpeta mods.
//  * Cualquier otro archivo que el juego abra por nombre (data/handling.cfg,
//    data/timecyc.dat, models/generic.txd, txd/splash1.txd, anim/ped.ifp,
//    text/american.gxt, ...): se abre el de la carpeta mods.
//
// Si dos mods traen el mismo archivo gana el primero en orden alfabetico de
// su ruta (renombra la carpeta para cambiar la prioridad, p. ej. "1_mimod").
// Las carpetas o archivos que empiezan con "." se ignoran (para desactivar
// un mod sin borrarlo basta con renombrarlo a ".mimod").
// ============================================================================
#include "common.h"

#if defined ANDROID
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <string>
#include <vector>
#include <algorithm>
#include "Streaming.h"
#include "ModelInfo.h"
#include "TxdStore.h"
#include "CdStream.h"

extern char *StorageRootBuffer;
bool gbModLoaderEnabled = true;

#define MODS_IMG_INDEX 0x7F          // "imagen" virtual para los archivos sueltos
#define MODS_FILE_SHIFT 13           // 13 bits de sector: hasta 16 MB por archivo
#define MODS_MAX_FILES (1 << (24 - MODS_FILE_SHIFT))

struct ModFile {
	std::string lower;   // nombre en minusculas (sin carpeta)
	std::string path;    // ruta completa
	uint32 size;
	int fd;
};
static std::vector<ModFile> sMods;
static bool sScanned = false;

static std::string
ToLower(const char *s)
{
	std::string r(s);
	for (auto &c : r) c = tolower((unsigned char)c);
	return r;
}

static void
ScanDir(const std::string &dir, int depth)
{
	if (depth > 8)
		return;
	DIR *d = opendir(dir.c_str());
	if (d == nil)
		return;
	struct dirent *e;
	while ((e = readdir(d)) != nil) {
		if (e->d_name[0] == '.')
			continue; // ".", ".." y mods desactivados
		std::string full = dir + "/" + e->d_name;
		struct stat st;
		if (stat(full.c_str(), &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode))
			ScanDir(full, depth + 1);
		else if (S_ISREG(st.st_mode))
			sMods.push_back({ ToLower(e->d_name), full, (uint32)st.st_size, -1 });
	}
	closedir(d);
}

void
ModLoaderInit(void)
{
	if (sScanned || StorageRootBuffer == nil)
		return; // (si aun no se conoce la carpeta del juego, se reintenta luego)
	sScanned = true;
	if (!gbModLoaderEnabled)
		return;
	ScanDir(std::string(StorageRootBuffer) + "/mods", 0);
	std::sort(sMods.begin(), sMods.end(), [](const ModFile &a, const ModFile &b) { return a.path < b.path; });
	// sin duplicados: gana el primero
	std::vector<ModFile> unique;
	for (auto &m : sMods) {
		bool dup = false;
		for (auto &u : unique)
			if (u.lower == m.lower) { dup = true; break; }
		if (dup)
			debug("ModLoader: '%s' ignorado (ya hay otro archivo con ese nombre)\n", m.path.c_str());
		else
			unique.push_back(m);
	}
	sMods.swap(unique);
	debug("ModLoader: %d archivos en mods/\n", (int)sMods.size());
}

// Ruta del reemplazo para un archivo que el juego quiere abrir para leer.
const char *
ModLoaderRedirect(const char *path, const char *mode)
{
	if (path == nil || (mode && (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'))))
		return nil;
	ModLoaderInit();
	if (sMods.empty())
		return nil;
	const char *base = path;
	for (const char *p = path; *p; p++)
		if (*p == '/' || *p == '\\')
			base = p + 1;
	std::string lower = ToLower(base);
	// nunca reemplazar configuracion, partidas ni las imagenes del streaming
	if (lower == "re3.ini" || lower == "gta3.set" || lower.compare(0, 6, "gta3sf") == 0 ||
	    lower == "gta3.img" || lower == "gta3.dir")
		return nil;
	for (auto &m : sMods)
		if (m.lower == lower)
			return m.path.c_str();
	return nil;
}

// Descriptor de archivo para el lector del streaming (CdStream_posix.cpp).
int
ModLoaderGetFd(uint32 fileIndex)
{
	if (fileIndex >= sMods.size())
		return -1;
	ModFile &m = sMods[fileIndex];
	if (m.fd < 0)
		m.fd = open(m.path.c_str(), O_RDONLY);
	return m.fd;
}

// Llamado justo despues de leer gta3.dir: apunta los modelos y texturas
// reemplazados a sus archivos sueltos.
void
ModLoaderApplyStreaming(int32 &streamingBufferSize)
{
	ModLoaderInit();
	int count = 0;
	for (uint32 i = 0; i < sMods.size() && i < MODS_MAX_FILES; i++) {
		ModFile &m = sMods[i];
		size_t dot = m.lower.rfind('.');
		if (dot == std::string::npos)
			continue;
		std::string ext = m.lower.substr(dot + 1);
		std::string name = m.lower.substr(0, dot);
		if (ext != "dff" && ext != "txd")
			continue;
		uint32 sectors = (m.size + CDSTREAM_SECTOR_SIZE - 1) / CDSTREAM_SECTOR_SIZE;
		if (sectors == 0 || sectors >= (1u << MODS_FILE_SHIFT)) {
			debug("ModLoader: '%s' es demasiado grande, se ignora\n", m.path.c_str());
			continue;
		}

		int32 streamId = -1;
		if (ext == "dff") {
			int32 modelId;
			if (CModelInfo::GetModelInfo(name.c_str(), &modelId))
				streamId = modelId;
		} else {
			int32 slot = CTxdStore::FindTxdSlot(name.c_str());
			if (slot == -1) {
				// solo reemplazos: un .txd suelto que no existe en el juego
				// (p. ej. generic.txd) se maneja por nombre de archivo
				continue;
			}
			streamId = slot + STREAM_OFFSET_TXD;
		}
		if (streamId < 0)
			continue;

		uint32 posn = (MODS_IMG_INDEX << 24) | (i << MODS_FILE_SHIFT);
		CStreaming::ms_aInfoForModel[streamId].SetCdPosnAndSize(posn, sectors);
		// ya no es contiguo con sus vecinos de gta3.img: no leerlos en bloque
		CStreaming::ms_aInfoForModel[streamId].m_nextID = -1;
		for (int32 j = 0; j < NUMSTREAMINFO; j++)
			if (CStreaming::ms_aInfoForModel[j].m_nextID == streamId)
				CStreaming::ms_aInfoForModel[j].m_nextID = -1;
		if ((int32)sectors > streamingBufferSize)
			streamingBufferSize = sectors;
		count++;
	}
	if (count)
		debug("ModLoader: %d modelos/texturas de gta3.img reemplazados\n", count);
}
#endif
