#pragma once
// [HUD] HudWidthScale / HudHeightScale (del Widescreen Fix de ThirteenAG):
// escala todo el HUD respecto de su esquina (reloj/dinero arriba a la derecha,
// radar abajo a la izquierda, etc.). Se incluye al final de los includes de
// Hud.cpp y Radar.cpp y redefine las macros de escala solo ahi.
#if defined ANDROID
#include "Frontend.h"
extern float gfHudScaleW, gfHudScaleH;
#ifdef MENU_MAP
// el mapa del menu de pausa usa el mismo codigo del radar: no se escala
#define HUD_SCALE_W (CMenuManager::bMenuMapActive ? 1.0f : gfHudScaleW)
#define HUD_SCALE_H (CMenuManager::bMenuMapActive ? 1.0f : gfHudScaleH)
#else
#define HUD_SCALE_W (gfHudScaleW)
#define HUD_SCALE_H (gfHudScaleH)
#endif
#undef SCREEN_SCALE_X
#undef SCREEN_SCALE_Y
#define SCREEN_SCALE_X(a) (SCREEN_SCALE_AR(SCREEN_STRETCH_X(a)) * HUD_SCALE_W)
#if defined(ASPECT_RATIO_SCALE) && defined(PROPER_SCALING) && !defined(FORCE_PC_SCALING) && !defined(NO_SWITCHABLE_SCALING)
#define SCREEN_SCALE_Y(a) (CDraw::ScaleY(SCREEN_STRETCH_Y(a)) * HUD_SCALE_H)
#else
#define SCREEN_SCALE_Y(a) (SCREEN_STRETCH_Y(a) * HUD_SCALE_H)
#endif
#endif
