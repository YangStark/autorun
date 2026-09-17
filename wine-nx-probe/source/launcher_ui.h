/*
 * Drawing and input for the launcher, on SDL2's renderer: the fixed Wine-NX
 * visual system, text, panels, the header and the footer of button hints, a list
 * of settings rows, dialogs, and controller, keyboard and touch input.
 */
#ifndef WINE_NX_LAUNCHER_UI_H
#define WINE_NX_LAUNCHER_UI_H

#include <SDL.h>
#include <SDL_ttf.h>

/* SDL names controller buttons by position: Nintendo's A, on the right, is SDL's B. */
enum ui_button
{
    UI_NONE = -1,
    UI_A = SDL_CONTROLLER_BUTTON_B,
    UI_B = SDL_CONTROLLER_BUTTON_A,
    UI_X = SDL_CONTROLLER_BUTTON_Y,
    UI_Y = SDL_CONTROLLER_BUTTON_X,
    UI_MINUS = SDL_CONTROLLER_BUTTON_BACK,
    UI_PLUS = SDL_CONTROLLER_BUTTON_START,
    UI_L = SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    UI_R = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    UI_UP = SDL_CONTROLLER_BUTTON_DPAD_UP,
    UI_DOWN = SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    UI_LEFT = SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    UI_RIGHT = SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
};

enum ui_touch
{
    UI_TOUCH_NONE,
    UI_TOUCH_TAP,
    UI_TOUCH_SCROLL_UP,    /* the finger moved up: show later rows */
    UI_TOUCH_SCROLL_DOWN,
    UI_TOUCH_SWIPE_LEFT,
    UI_TOUCH_SWIPE_RIGHT,
};

/* One input: a button (controller, keyboard or a tapped footer hint), or a touch. */
struct ui_input
{
    int button;          /* enum ui_button */
    enum ui_touch touch;
    int x, y;            /* where a touch ended, in screen pixels */
    int steps;           /* rows a scroll moves */
};

#define UI_TEXT_CACHE    192
#define UI_TEXT_KEY      160
#define UI_FOOTER_HINTS  10
#define UI_HEADER_HEIGHT 80
#define UI_FOOTER_Y      (720 - 26)

struct ui_hint
{
    int button;         /* enum ui_button, or UI_NONE for a label only */
    const char *label;
};

struct ui_text_entry
{
    TTF_Font *font;
    SDL_Color color;
    char text[UI_TEXT_KEY];
    SDL_Texture *texture;
    int width, height;
    unsigned int use;
};

struct ui
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    int width, height;
    TTF_Font *small, *normal, *large;
    SDL_Texture *glow, *sheen;
    SDL_Texture *glyphs[16];

    int animations;
    SDL_Color background, text, dim, value, selection, panel, card, focus, danger;

    struct ui_text_entry cache[UI_TEXT_CACHE];
    unsigned int cache_use;

    SDL_GameController *controller;
    int held;
    Uint32 held_since, held_last;
    int stick_x, stick_y;
    struct
    {
        int active, vertical;
        SDL_FingerID finger;
        float start_x, start_y, last_y;
        Uint32 started;
    } touch;

    SDL_Rect footer_hits[UI_FOOTER_HINTS];
    int footer_buttons[UI_FOOTER_HINTS];
    int footer_count;

    Uint32 fx_start, busy_until, deadline;
    float highlight, last_highlight;
    int scrolling_text;
    SDL_Event queued[32];
    int queued_count;
    int running;

    char toast[160];
    Uint32 toast_until;
};

int  ui_init( struct ui *ui, const void *font_data, size_t font_size, int animations );
void ui_quit( struct ui *ui );
/* Why ui_init failed. */
const char *ui_error(void);
/* Whether SDL got as far as a window, so the screen was in EGL's hands. */
int  ui_screen_used(void);
/* Whether transitions should keep scheduling frames. */
int  ui_animated( const struct ui *ui );

/* A frame: ui_begin_frame, ui_poll until it returns 0, draw, ui_present, ui_wait. */
int  ui_begin_frame( struct ui *ui );
int  ui_poll( struct ui *ui, struct ui_input *input );
void ui_present( struct ui *ui );
/* Called with each finished frame before it is shown; the host test takes screenshots here. */
extern void (*ui_present_hook)( SDL_Renderer *renderer );
void ui_wait( struct ui *ui );
void ui_start_screen( struct ui *ui );

void ui_fill( struct ui *ui, int x, int y, int w, int h, SDL_Color color );
void ui_border( struct ui *ui, int x, int y, int w, int h, int thickness, SDL_Color color );
void ui_fill_circle( struct ui *ui, float cx, float cy, float radius, SDL_Color color );
void ui_rounded( struct ui *ui, int x, int y, int w, int h, int radius, SDL_Color color );
void ui_panel( struct ui *ui, int x, int y, int w, int h );
void ui_background( struct ui *ui );
/* A rectangle shading from one colour at its top (or, when horizontal, its left) to another. */
void ui_gradient( struct ui *ui, int x, int y, int w, int h, SDL_Color from, SDL_Color to, int horizontal );
/* A texture inside rounded corners, tinted by mod: covers, and the light over them.
 * src is the part of the texture to use, or NULL for all of it. */
void ui_rounded_texture( struct ui *ui, SDL_Texture *texture, const SDL_Rect *src, SDL_Rect rect, int radius,
                         SDL_Color mod );
/* White fading to nothing downwards, to stretch over a shape as a sheen. */
SDL_Texture *ui_sheen( struct ui *ui );
/* A white icon from an SVG path (launcher_svg.h), size pixels square, to tint with
 * SDL_SetTextureColorMod and SDL_SetTextureAlphaMod; NULL when it cannot be made. */
SDL_Texture *ui_svg_texture( struct ui *ui, const char *d, float view_x, float view_y, float view_w, float view_h,
                             int size );

int  ui_text_width( struct ui *ui, TTF_Font *font, const char *text );
void ui_text( struct ui *ui, TTF_Font *font, int x, int y, const char *text, SDL_Color color );
void ui_text_centered( struct ui *ui, TTF_Font *font, int cx, int y, const char *text, SDL_Color color );
void ui_text_right( struct ui *ui, TTF_Font *font, int right, int y, const char *text, SDL_Color color );
/* Text cut to max_width with an ellipsis, or, when scroll is set, moving back and forth. */
void ui_text_fit( struct ui *ui, TTF_Font *font, int x, int y, int max_width, const char *text,
                  SDL_Color color, int scroll );
int  ui_text_wrapped( struct ui *ui, TTF_Font *font, int x, int y, int max_width, int max_lines,
                      const char *text, SDL_Color color, int centered );

void ui_header( struct ui *ui, const char *title, const char *context );
void ui_footer( struct ui *ui, const struct ui_hint *hints, int count );
/* The same hints, ending at right on the line through y, and tappable like the footer's. */
void ui_hints_right( struct ui *ui, const struct ui_hint *hints, int count, int right, int y );
void ui_fade( struct ui *ui );
void ui_toast( struct ui *ui, const char *text, int milliseconds );
void ui_draw_toast( struct ui *ui );
/* Move the highlight toward target_y; returns where to draw it. */
float ui_highlight( struct ui *ui, float target_y );

/* A card of text over the current screen, closed by A or B. */
void ui_message( struct ui *ui, const char *title, const char *text );
/* A question answered with A (returns 1) or B (returns 0). */
int  ui_confirm( struct ui *ui, const char *title, const char *text, const char *yes );

/* A list of settings rows. ui_list_run draws it and handles input until the
 * user acts on a row, then returns the action for the row at list->selection;
 * the caller changes what it must and calls it again. */
struct ui_row
{
    char label[96];
    char value[192];
    int disabled;
    int adjustable;     /* Left and Right change the value */
    int destructive;
    const char *help;   /* shown by X */
};

struct ui_list
{
    int selection, top;
    int started;
};

enum ui_action
{
    UI_ACTION_BACK,
    UI_ACTION_CHOOSE,
    UI_ACTION_LEFT,
    UI_ACTION_RIGHT,
    UI_ACTION_RESET,
    UI_ACTION_QUIT,     /* the system asked the launcher to close */
};

enum ui_action ui_list_run( struct ui *ui, struct ui_list *list, const char *title, const char *context,
                            const struct ui_row *rows, int count, int can_reset );

#endif
