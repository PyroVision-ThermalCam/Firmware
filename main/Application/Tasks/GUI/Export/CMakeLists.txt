SET(SOURCES screens/ui_Splash.c
    screens/ui_Main.c
    screens/ui_Menu.c
    screens/ui_Info.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    ui_events.cpp
    images/ui_img_logo_80x44_png.c
    images/ui_img_text_218x40_png.c
    fonts/ui_font_fa12.c
    fonts/ui_font_fa16.c)

add_library(ui ${SOURCES})
