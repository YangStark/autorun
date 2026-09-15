/*
 * Drawing and input for the launcher (launcher_ui.h).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "launcher_ui.h"

#define FADE_MS          160
#define REPEAT_DELAY_MS  360
#define REPEAT_MS        85
#define STICK_PRESS      18000
#define STICK_RELEASE    8000
#define LIST_TOP         118
#define ROW_HEIGHT       46

enum glyph
{
    GLYPH_A, GLYPH_B, GLYPH_X, GLYPH_Y, GLYPH_PLUS, GLYPH_MINUS, GLYPH_L, GLYPH_R, GLYPH_LEFT, GLYPH_RIGHT,
    GLYPH_UP, GLYPH_DOWN, GLYPH_COUNT
};

struct theme_colors
{
    const char *name;
    int animated;
    SDL_Color background, text, dim, value, selection, panel, card, focus;
};

static const struct theme_colors themes[UI_THEME_COUNT] =
{
    [UI_THEME_BUBBLES] = { "Bubbles", 1, { 3, 82, 120, 255 }, { 245, 252, 255, 255 }, { 187, 229, 243, 255 },
                           { 255, 255, 255, 255 }, { 111, 224, 249, 255 }, { 0, 67, 101, 180 },
                           { 2, 75, 110, 207 }, { 17, 133, 169, 218 } },
    [UI_THEME_GLOW]    = { "Glow", 1, { 8, 12, 24, 255 }, { 235, 239, 247, 255 }, { 151, 163, 184, 255 },
                           { 255, 215, 120, 255 }, { 116, 200, 255, 255 }, { 16, 23, 39, 184 },
                           { 22, 30, 49, 214 }, { 28, 69, 92, 208 } },
    [UI_THEME_CLASSIC] = { "Classic", 0, { 22, 24, 30, 255 }, { 228, 230, 235, 255 }, { 150, 155, 165, 255 },
                           { 255, 210, 100, 255 }, { 255, 170, 0, 255 }, { 28, 31, 40, 255 },
                           { 24, 26, 34, 255 }, { 66, 56, 30, 235 } },
    [UI_THEME_OLED]    = { "OLED", 0, { 0, 0, 0, 255 }, { 245, 247, 249, 255 }, { 145, 151, 158, 255 },
                           { 255, 255, 255, 255 }, { 0, 210, 190, 255 }, { 4, 4, 5, 248 },
                           { 8, 8, 10, 250 }, { 0, 58, 53, 245 } },
};

static char last_error[256];
static int window_created;

const char *ui_error(void)
{
    return last_error;
}

int ui_screen_used(void)
{
    return window_created;
}

static float clampf( float v, float lo, float hi )
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int platform_running(void)
{
#ifdef __SWITCH__
    return appletMainLoop();
#else
    return 1;
#endif
}

const char *ui_theme_name( enum ui_theme theme )
{
    return themes[theme].name;
}

void ui_set_theme( struct ui *ui, enum ui_theme theme )
{
    const struct theme_colors *colors = &themes[theme < UI_THEME_COUNT ? theme : UI_THEME_BUBBLES];

    ui->theme = colors - themes;
    ui->background = colors->background;
    ui->text = colors->text;
    ui->dim = colors->dim;
    ui->value = colors->value;
    ui->selection = colors->selection;
    ui->panel = colors->panel;
    ui->card = colors->card;
    ui->focus = colors->focus;
    ui->danger = (SDL_Color){ 255, 120, 120, 255 };
}

int ui_animated( const struct ui *ui )
{
    return ui->animations && themes[ui->theme].animated;
}

void ui_fill( struct ui *ui, int x, int y, int w, int h, SDL_Color color )
{
    SDL_Rect rect = { x, y, w, h };

    SDL_SetRenderDrawColor( ui->renderer, color.r, color.g, color.b, color.a );
    SDL_RenderFillRect( ui->renderer, &rect );
}

void ui_border( struct ui *ui, int x, int y, int w, int h, int thickness, SDL_Color color )
{
    int i;

    SDL_SetRenderDrawColor( ui->renderer, color.r, color.g, color.b, color.a );
    for (i = 0; i < thickness; i++)
    {
        SDL_Rect rect = { x - i, y - i, w + 2 * i, h + 2 * i };
        SDL_RenderDrawRect( ui->renderer, &rect );
    }
}

/* A pie of a circle from angle a0 to a1 as triangles. */
static void fill_arc( struct ui *ui, float cx, float cy, float radius, float a0, float a1, SDL_Color color )
{
    SDL_Vertex vertices[3 * 32];
    int i, segments = 32;

    for (i = 0; i < segments; i++)
    {
        float s = a0 + (a1 - a0) * i / segments, e = a0 + (a1 - a0) * (i + 1) / segments;
        SDL_Vertex *v = vertices + 3 * i;

        v[0] = (SDL_Vertex){ { cx, cy }, color, { 0, 0 } };
        v[1] = (SDL_Vertex){ { cx + cos( s ) * radius, cy + sin( s ) * radius }, color, { 0, 0 } };
        v[2] = (SDL_Vertex){ { cx + cos( e ) * radius, cy + sin( e ) * radius }, color, { 0, 0 } };
    }
    SDL_RenderGeometry( ui->renderer, NULL, vertices, 3 * segments, NULL, 0 );
}

void ui_fill_circle( struct ui *ui, float cx, float cy, float radius, SDL_Color color )
{
    fill_arc( ui, cx, cy, radius, 0, 2 * M_PI, color );
}

void ui_rounded( struct ui *ui, int x, int y, int w, int h, int radius, SDL_Color color )
{
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    ui_fill( ui, x + radius, y, w - 2 * radius, h, color );
    ui_fill( ui, x, y + radius, radius, h - 2 * radius, color );
    ui_fill( ui, x + w - radius, y + radius, radius, h - 2 * radius, color );
    fill_arc( ui, x + radius, y + radius, radius, M_PI, 1.5 * M_PI, color );
    fill_arc( ui, x + w - radius, y + radius, radius, 1.5 * M_PI, 2 * M_PI, color );
    fill_arc( ui, x + w - radius, y + h - radius, radius, 0, 0.5 * M_PI, color );
    fill_arc( ui, x + radius, y + h - radius, radius, 0.5 * M_PI, M_PI, color );
}

void ui_panel( struct ui *ui, int x, int y, int w, int h )
{
    ui_fill( ui, x, y, w, h, ui->panel );
    ui_border( ui, x, y, w, h, 1, (SDL_Color){ 255, 255, 255, ui_animated( ui ) ? 28 : 16 } );
}

/* A white disc fading out from its centre, tinted and stretched for glows, light and bubbles. */
static SDL_Texture *make_glow( struct ui *ui )
{
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat( 0, 256, 256, 32, SDL_PIXELFORMAT_RGBA32 );
    SDL_Texture *texture = NULL;
    int x, y;

    if (!surface) return NULL;
    SDL_LockSurface( surface );
    for (y = 0; y < 256; y++)
    {
        Uint8 *row = (Uint8 *)surface->pixels + y * surface->pitch;

        for (x = 0; x < 256; x++)
        {
            float dx = (x - 127.5f) / 128, dy = (y - 127.5f) / 128, d = sqrt( dx * dx + dy * dy );
            float strength = d >= 1 ? 0 : 1 - d;

            row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = 255;
            row[x * 4 + 3] = 255 * strength * strength;
        }
    }
    SDL_UnlockSurface( surface );
    if ((texture = SDL_CreateTextureFromSurface( ui->renderer, surface )))
        SDL_SetTextureBlendMode( texture, SDL_BLENDMODE_BLEND );
    SDL_FreeSurface( surface );
    return texture;
}

/* A controller button: a dark disc (or pill for L and R) with a light label,
 * drawn three times larger than shown so its edge stays smooth when scaled down. */
static SDL_Texture *make_glyph( struct ui *ui, const char *label, int pill )
{
    const int scale = 3, base = TTF_FontHeight( ui->small ) + 6;
    int height = base * scale, width = (pill ? base * 8 / 5 : base) * scale;
    SDL_Texture *texture = SDL_CreateTexture( ui->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                              width, height );
    static const SDL_Color layers[3] = { { 14, 16, 22, 255 }, { 92, 99, 114, 255 }, { 52, 57, 68, 255 } };
    SDL_Surface *surface;
    int i;

    if (!texture) return NULL;
    SDL_SetTextureBlendMode( texture, SDL_BLENDMODE_BLEND );
    SDL_SetRenderTarget( ui->renderer, texture );
    SDL_SetRenderDrawColor( ui->renderer, 0, 0, 0, 0 );
    SDL_RenderClear( ui->renderer );
    for (i = 0; i < 3; i++)
    {
        int inset = i * scale;

        if (pill) ui_rounded( ui, inset, inset, width - 2 * inset, height - 2 * inset, height / 2 - inset, layers[i] );
        else ui_fill_circle( ui, width / 2.0f, height / 2.0f, height / 2.0f - inset, layers[i] );
    }
    if ((surface = TTF_RenderUTF8_Blended( ui->large, label, (SDL_Color){ 246, 248, 252, 255 } )))
    {
        SDL_Texture *text = SDL_CreateTextureFromSurface( ui->renderer, surface );
        int h = height * 56 / 100, w = surface->w * h / (surface->h ? surface->h : 1);
        SDL_Rect dst = { (width - w) / 2, (height - h) / 2, w, h };

        if (text)
        {
            SDL_RenderCopy( ui->renderer, text, NULL, &dst );
            SDL_DestroyTexture( text );
        }
        SDL_FreeSurface( surface );
    }
    SDL_SetRenderTarget( ui->renderer, NULL );
    return texture;
}

static TTF_Font *open_font( const void *data, size_t size, int points )
{
    SDL_RWops *stream = SDL_RWFromConstMem( data, (int)size );

    return stream ? TTF_OpenFontRW( stream, 1, points ) : NULL;
}

int ui_init( struct ui *ui, const void *font_data, size_t font_size, enum ui_theme theme, int animations )
{
    static const struct { const char *label; int pill; } glyphs[GLYPH_COUNT] =
    {
        [GLYPH_A] = { "A", 0 }, [GLYPH_B] = { "B", 0 }, [GLYPH_X] = { "X", 0 }, [GLYPH_Y] = { "Y", 0 },
        [GLYPH_PLUS] = { "+", 0 }, [GLYPH_MINUS] = { "-", 0 }, [GLYPH_L] = { "L", 1 }, [GLYPH_R] = { "R", 1 },
        [GLYPH_LEFT] = { "<", 0 }, [GLYPH_RIGHT] = { ">", 0 }, [GLYPH_UP] = { "^", 0 }, [GLYPH_DOWN] = { "v", 0 },
    };
    Uint32 flags = 0;
    int i;

    memset( ui, 0, sizeof(*ui) );
    ui->width = 1280;
    ui->height = 720;
    ui->animations = animations;
    ui->running = 1;
    ui->highlight = -1;
    ui_set_theme( ui, theme );

    SDL_SetMainReady();
    SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, "linear" );
    SDL_SetHint( SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1" );
    if (SDL_Init( SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS ))
    {
        snprintf( last_error, sizeof(last_error), "%s", SDL_GetError() );
        return 0;
    }
    if (TTF_Init()) goto fail;
#ifdef __SWITCH__
    flags = SDL_WINDOW_FULLSCREEN;
#endif
    if (!(ui->window = SDL_CreateWindow( "Wine-NX", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                         ui->width, ui->height, flags ))) goto fail;
    window_created = 1;
    /* SDL's software renderer draws the same, only slower, if the GPU one cannot start. */
    if (!(ui->renderer = SDL_CreateRenderer( ui->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC )) &&
        !(ui->renderer = SDL_CreateRenderer( ui->window, -1, SDL_RENDERER_SOFTWARE )))
        goto fail;
    SDL_RenderSetLogicalSize( ui->renderer, ui->width, ui->height );
    SDL_SetRenderDrawBlendMode( ui->renderer, SDL_BLENDMODE_BLEND );
    if (!(ui->small = open_font( font_data, font_size, 20 )) || !(ui->normal = open_font( font_data, font_size, 26 )) ||
        !(ui->large = open_font( font_data, font_size, 40 ))) goto fail;
    ui->glow = make_glow( ui );
    for (i = 0; i < GLYPH_COUNT; i++) ui->glyphs[i] = make_glyph( ui, glyphs[i].label, glyphs[i].pill );
    for (i = 0; i < SDL_NumJoysticks() && !ui->controller; i++)
        if (SDL_IsGameController( i )) ui->controller = SDL_GameControllerOpen( i );
    return 1;

fail:
    snprintf( last_error, sizeof(last_error), "%s", SDL_GetError() );
    ui_quit( ui );
    return 0;
}

void ui_quit( struct ui *ui )
{
    int i;

    for (i = 0; i < UI_TEXT_CACHE; i++)
        if (ui->cache[i].texture) SDL_DestroyTexture( ui->cache[i].texture );
    for (i = 0; i < GLYPH_COUNT; i++)
        if (ui->glyphs[i]) SDL_DestroyTexture( ui->glyphs[i] );
    if (ui->glow) SDL_DestroyTexture( ui->glow );
    if (ui->small) TTF_CloseFont( ui->small );
    if (ui->normal) TTF_CloseFont( ui->normal );
    if (ui->large) TTF_CloseFont( ui->large );
    if (ui->controller) SDL_GameControllerClose( ui->controller );
    if (ui->renderer) SDL_DestroyRenderer( ui->renderer );
    if (ui->window) SDL_DestroyWindow( ui->window );
    if (TTF_WasInit()) TTF_Quit();
    SDL_Quit();
    memset( ui, 0, sizeof(*ui) );
}

/***********************************************************************
 * Backgrounds
 */

static void draw_glow( struct ui *ui, SDL_Rect rect, SDL_Color color, double angle )
{
    if (!ui->glow) return;
    SDL_SetTextureColorMod( ui->glow, color.r, color.g, color.b );
    SDL_SetTextureAlphaMod( ui->glow, color.a );
    SDL_RenderCopyEx( ui->renderer, ui->glow, NULL, &rect, angle, NULL, SDL_FLIP_NONE );
}

/* Tiny lights drifting sideways and pulsing. */
static void draw_sparkles( struct ui *ui, float t, int count, float speed, SDL_Color color, int alpha )
{
    int i;

    for (i = 0; i < count; i++)
    {
        float x = fmod( i * 0.371f + t * speed * (0.65f + (i % 5) * 0.11f), 1.12f ) - 0.06f;
        float y = fmod( i * 0.217f + 0.11f * sin( t * 0.29f + i * 1.73f ) + 1.0f, 1.0f );
        float pulse = 0.45f + 0.55f * sin( t * (0.9f + (i % 4) * 0.17f) + i );
        int size = i % 9 ? 2 : 3;

        color.a = alpha * (0.55f + 0.45f * pulse);
        ui_fill( ui, x * ui->width, y * ui->height, size, size, color );
    }
}

/* A light blue sea: a gradient, light shafts from the surface and rising bubbles. */
static void draw_bubbles( struct ui *ui, float t )
{
    static const SDL_Color top = { 70, 198, 229, 255 }, middle = { 15, 147, 193, 255 }, bottom = { 3, 82, 120, 255 };
    const int bands = 48;
    int i, s;

    for (i = 0; i < bands; i++)
    {
        float y = (i + 0.5f) / bands, k;
        const SDL_Color *a, *b;
        int y0 = i * ui->height / bands, y1 = (i + 1) * ui->height / bands;

        if (y < 0.58f) { a = &top; b = &middle; k = y / 0.58f; }
        else { a = &middle; b = &bottom; k = (y - 0.58f) / 0.42f; }
        ui_fill( ui, 0, y0, ui->width, y1 - y0,
                 (SDL_Color){ a->r + (b->r - a->r) * k, a->g + (b->g - a->g) * k, a->b + (b->b - a->b) * k, 255 } );
    }
    draw_glow( ui, (SDL_Rect){ -ui->width / 6, -ui->height / 3, ui->width * 4 / 3, ui->height * 2 / 3 },
               (SDL_Color){ 197, 244, 255, 96 }, 0 );
    for (i = 0; i < 7; i++)
    {
        float sway = sin( t * (0.10f + i * 0.013f) + i * 1.31f );
        int w = ui->width * (11 + (i % 3) * 3) / 100;
        int x = ui->width * (8 + i * 14) / 100 + sway * ui->width * 0.025f - w / 2;

        draw_glow( ui, (SDL_Rect){ x, -ui->height / 3, w, ui->height * 4 / 3 },
                   (SDL_Color){ 197, 244, 255, 23 + (i % 3) * 7 }, -9.0 + i * 2.7 + sway * 2.0 );
    }
    for (i = 0; i < 18; i++)
    {
        float y = 1.08f - fmod( i * 0.173f + t * (0.038f + (i % 5) * 0.007f), 1.18f );
        float x = 0.05f + fmod( i * 0.283f, 0.90f ) + 0.032f * sin( t * (0.31f + (i % 4) * 0.04f) + i );
        float fade = clampf( (1.10f - y) * 5, 0, 1 ), fade_top = clampf( (y + 0.12f) * 6, 0, 1 );
        float radius = ui->height * (0.009f + (i % 6) * 0.0042f) * (i % 11 ? 1.0f : 1.5f);
        int alpha = (fade < fade_top ? fade : fade_top) * (85 + (i % 4) * 24);
        float cx = x * ui->width, cy = y * ui->height;
        SDL_FPoint ring[25], shine[6];

        if (alpha <= 0) continue;
        draw_glow( ui, (SDL_Rect){ cx - radius * 2, cy - radius * 2, radius * 4, radius * 4 },
                   (SDL_Color){ 180, 237, 255, alpha / 5 }, 0 );
        SDL_SetRenderDrawColor( ui->renderer, 188, 240, 255, alpha );
        for (s = 0; s < 2; s++)
        {
            int j;
            for (j = 0; j <= 24; j++)
                ring[j] = (SDL_FPoint){ cx + cos( j * 2 * M_PI / 24 ) * (radius - s), cy + sin( j * 2 * M_PI / 24 ) * (radius - s) };
            SDL_RenderDrawLinesF( ui->renderer, ring, 25 );
        }
        for (s = 0; s < 6; s++)
            shine[s] = (SDL_FPoint){ cx + cos( 3.55f + s * 0.13f ) * radius, cy + sin( 3.55f + s * 0.13f ) * radius };
        SDL_SetRenderDrawColor( ui->renderer, 235, 252, 255, alpha + 55 > 255 ? 255 : alpha + 55 );
        SDL_RenderDrawLinesF( ui->renderer, shine, 6 );
    }
    draw_sparkles( ui, t, 24, 0.008f, (SDL_Color){ 216, 246, 255, 255 }, 62 );
}

/* Night blue with slow colour clouds. */
static void draw_glows( struct ui *ui, float t )
{
    static const struct { float x, y, radius; SDL_Color color; } glows[4] =
    {
        { 0.10f, 0.20f, 0.90f, { 45, 140, 255, 128 } },
        { 0.84f, 0.34f, 0.78f, { 154, 75, 255, 112 } },
        { 0.54f, 0.91f, 0.94f, { 0, 210, 190, 94 } },
        { 0.42f, 0.48f, 0.58f, { 64, 125, 255, 67 } },
    };
    int i;

    for (i = 0; i < 4; i++)
    {
        float x = glows[i].x + 0.12f * sin( t * (0.43f - i * 0.05f) + i * 1.7f );
        float y = glows[i].y + 0.10f * cos( t * (0.37f - i * 0.03f) + i );
        int d = ui->height * glows[i].radius;

        draw_glow( ui, (SDL_Rect){ x * ui->width - d / 2, y * ui->height - d / 2, d, d }, glows[i].color, 0 );
    }
    draw_sparkles( ui, t, 28, 0.011f, (SDL_Color){ 182, 224, 255, 255 }, 88 );
}

void ui_background( struct ui *ui )
{
    float t = ui->animations ? SDL_GetTicks() / 1000.0f : 0;

    SDL_RenderSetClipRect( ui->renderer, NULL );
    SDL_SetRenderDrawColor( ui->renderer, ui->background.r, ui->background.g, ui->background.b, 255 );
    SDL_RenderClear( ui->renderer );
    if (ui->theme == UI_THEME_BUBBLES) draw_bubbles( ui, t );
    else if (ui->theme == UI_THEME_GLOW) draw_glows( ui, t );
}

/***********************************************************************
 * Text
 */

static struct ui_text_entry *text_entry( struct ui *ui, TTF_Font *font, const char *text, SDL_Color color, int any_color )
{
    struct ui_text_entry *entry, *oldest = ui->cache;
    SDL_Surface *surface;
    int i;

    if (strlen( text ) >= UI_TEXT_KEY) return NULL;
    for (i = 0; i < UI_TEXT_CACHE; i++)
    {
        entry = ui->cache + i;
        if (entry->texture && entry->font == font && (any_color || !memcmp( &entry->color, &color, sizeof(color) )) &&
            !strcmp( entry->text, text ))
        {
            entry->use = ++ui->cache_use;
            return entry;
        }
        if (entry->use < oldest->use) oldest = entry;
    }
    if (any_color) return NULL;
    if (!(surface = TTF_RenderUTF8_Blended( font, text, color ))) return NULL;
    if (oldest->texture) SDL_DestroyTexture( oldest->texture );
    oldest->texture = SDL_CreateTextureFromSurface( ui->renderer, surface );
    oldest->width = surface->w;
    oldest->height = surface->h;
    SDL_FreeSurface( surface );
    if (!oldest->texture) return NULL;
    oldest->font = font;
    oldest->color = color;
    strcpy( oldest->text, text );
    oldest->use = ++ui->cache_use;
    return oldest;
}

int ui_text_width( struct ui *ui, TTF_Font *font, const char *text )
{
    struct ui_text_entry *entry;
    int w = 0, h;

    if (!text[0]) return 0;
    if ((entry = text_entry( ui, font, text, ui->text, 1 ))) return entry->width;
    TTF_SizeUTF8( font, text, &w, &h );
    return w;
}

void ui_text( struct ui *ui, TTF_Font *font, int x, int y, const char *text, SDL_Color color )
{
    struct ui_text_entry *entry;
    SDL_Surface *surface;

    if (!text[0]) return;
    if ((entry = text_entry( ui, font, text, color, 0 )))
    {
        SDL_Rect dst = { x, y, entry->width, entry->height };
        SDL_RenderCopy( ui->renderer, entry->texture, NULL, &dst );
        return;
    }
    /* Too long to cache. */
    if ((surface = TTF_RenderUTF8_Blended( font, text, color )))
    {
        SDL_Texture *texture = SDL_CreateTextureFromSurface( ui->renderer, surface );
        SDL_Rect dst = { x, y, surface->w, surface->h };

        if (texture)
        {
            SDL_RenderCopy( ui->renderer, texture, NULL, &dst );
            SDL_DestroyTexture( texture );
        }
        SDL_FreeSurface( surface );
    }
}

void ui_text_centered( struct ui *ui, TTF_Font *font, int cx, int y, const char *text, SDL_Color color )
{
    ui_text( ui, font, cx - ui_text_width( ui, font, text ) / 2, y, text, color );
}

void ui_text_right( struct ui *ui, TTF_Font *font, int right, int y, const char *text, SDL_Color color )
{
    ui_text( ui, font, right - ui_text_width( ui, font, text ), y, text, color );
}

/* Bytes of text that fit in max_width pixels, on a character boundary. */
static size_t fitting_bytes( TTF_Font *font, const char *text, int max_width )
{
    int extent, count;
    size_t bytes = 0;

    if (max_width <= 0 || TTF_MeasureUTF8( font, text, max_width, &extent, &count )) return 0;
    while (text[bytes] && count > 0)
    {
        bytes++;
        while ((text[bytes] & 0xc0) == 0x80) bytes++;
        count--;
    }
    return bytes;
}

static const char *ellipsis( TTF_Font *font )
{
    return TTF_GlyphIsProvided32( font, 0x2026 ) ? "\xe2\x80\xa6" : "...";
}

void ui_text_fit( struct ui *ui, TTF_Font *font, int x, int y, int max_width, const char *text,
                  SDL_Color color, int scroll )
{
    int width = ui_text_width( ui, font, text );
    char cut[UI_TEXT_KEY];
    size_t bytes;

    if (width <= max_width)
    {
        ui_text( ui, font, x, y, text, color );
        return;
    }
    if (scroll)
    {
        /* Out and back over 5 seconds, resting at both ends. */
        float phase = (SDL_GetTicks() % 5000) / 5000.0f, pos = clampf( (phase < 0.5f ? phase : 1 - phase) * 2.6f - 0.15f, 0, 1 );
        SDL_Rect clip = { x, y - 2, max_width, TTF_FontHeight( font ) + 8 };

        SDL_RenderSetClipRect( ui->renderer, &clip );
        ui_text( ui, font, x - (int)(pos * (width - max_width)), y, text, color );
        SDL_RenderSetClipRect( ui->renderer, NULL );
        ui->scrolling_text = 1;
        return;
    }
    bytes = fitting_bytes( font, text, max_width - ui_text_width( ui, font, ellipsis( font ) ) );
    if (bytes > sizeof(cut) - 4) bytes = sizeof(cut) - 4;
    while (bytes && text[bytes - 1] == ' ') bytes--;
    memcpy( cut, text, bytes );
    strcpy( cut + bytes, ellipsis( font ) );
    ui_text( ui, font, x, y, cut, color );
}

/* Break text into lines at spaces and newlines; the last line that fits is cut
 * with an ellipsis. Returns the number of lines, drawn only when draw is set. */
static int wrap_text( struct ui *ui, TTF_Font *font, int x, int y, int max_width, int max_lines,
                      const char *text, SDL_Color color, int centered, int draw )
{
    int lines = 0, line_height = TTF_FontHeight( font ) + 4;
    char line[UI_TEXT_KEY];

    while (*text && lines < max_lines)
    {
        const char *newline = strchr( text, '\n' );
        size_t len = newline ? (size_t)(newline - text) : strlen( text ), bytes;

        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy( line, text, len );
        line[len] = 0;
        bytes = fitting_bytes( font, line, max_width );
        if (bytes < len && lines + 1 < max_lines)
        {
            size_t space = bytes;

            while (space > 0 && line[space] != ' ') space--;
            if (space > 0) bytes = space;
            if (!bytes)
                do bytes++; while ((line[bytes] & 0xc0) == 0x80);
            line[bytes] = 0;
            text += bytes;
            while (*text == ' ') text++;
        }
        else
        {
            text += len;
            if (*text == '\n') text++;
        }
        if (draw)
        {
            int w = ui_text_width( ui, font, line );

            if (w > max_width) w = max_width;
            ui_text_fit( ui, font, centered ? x - w / 2 : x, y, max_width, line, color, 0 );
        }
        y += line_height;
        lines++;
    }
    return lines;
}

int ui_text_wrapped( struct ui *ui, TTF_Font *font, int x, int y, int max_width, int max_lines,
                     const char *text, SDL_Color color, int centered )
{
    return wrap_text( ui, font, x, y, max_width, max_lines, text, color, centered, 1 );
}

/***********************************************************************
 * Header, footer and overlays
 */

void ui_header( struct ui *ui, const char *title, const char *context )
{
    const int band = UI_HEADER_HEIGHT - 4;
    int title_right;

    ui_fill( ui, 0, 0, ui->width, band, ui->panel );
    if (!ui_animated( ui )) ui_fill( ui, 0, band, ui->width, 2, ui->selection );
    ui_text( ui, ui->normal, 28, (band - TTF_FontHeight( ui->normal )) / 2, "Wine-NX", ui->value );
    ui_text_centered( ui, ui->large, ui->width / 2, (band - TTF_FontHeight( ui->large )) / 2, title, ui->value );
    title_right = ui->width / 2 + ui_text_width( ui, ui->large, title ) / 2;
    if (context && context[0])
    {
        int max_width = ui->width - 28 - title_right - 30, width = ui_text_width( ui, ui->small, context );

        if (max_width > 40)
            ui_text_fit( ui, ui->small, ui->width - 28 - (width < max_width ? width : max_width),
                         (band - TTF_FontHeight( ui->small )) / 2, max_width, context, ui->dim, 1 );
    }
}

static SDL_Texture *button_glyph( struct ui *ui, int button )
{
    switch (button)
    {
    case UI_A: return ui->glyphs[GLYPH_A];
    case UI_B: return ui->glyphs[GLYPH_B];
    case UI_X: return ui->glyphs[GLYPH_X];
    case UI_Y: return ui->glyphs[GLYPH_Y];
    case UI_PLUS: return ui->glyphs[GLYPH_PLUS];
    case UI_MINUS: return ui->glyphs[GLYPH_MINUS];
    case UI_L: return ui->glyphs[GLYPH_L];
    case UI_R: return ui->glyphs[GLYPH_R];
    case UI_LEFT: return ui->glyphs[GLYPH_LEFT];
    case UI_RIGHT: return ui->glyphs[GLYPH_RIGHT];
    case UI_UP: return ui->glyphs[GLYPH_UP];
    case UI_DOWN: return ui->glyphs[GLYPH_DOWN];
    }
    return NULL;
}

void ui_footer( struct ui *ui, const struct ui_hint *hints, int count )
{
    const int glyph_gap = 10, label_gap = 8, pair_gap = 26, y = ui->height - 26;
    int i, x, total = 0, widths[UI_FOOTER_HINTS], heights[UI_FOOTER_HINTS];

    if (count > UI_FOOTER_HINTS) count = UI_FOOTER_HINTS;
    for (i = 0; i < count; i++)
    {
        SDL_Texture *glyph = button_glyph( ui, hints[i].button );

        widths[i] = heights[i] = 0;
        if (glyph)
        {
            SDL_QueryTexture( glyph, NULL, NULL, &widths[i], &heights[i] );
            widths[i] /= 3;
            heights[i] /= 3;
        }
        total += widths[i];
        if (hints[i].label && hints[i].label[0])
            total += (glyph ? label_gap : 0) + ui_text_width( ui, ui->small, hints[i].label ) + pair_gap;
        else total += glyph_gap;
    }
    if (count) total -= hints[count - 1].label && hints[count - 1].label[0] ? pair_gap : glyph_gap;

    x = (ui->width - total) / 2;
    ui->footer_count = 0;
    for (i = 0; i < count; i++)
    {
        SDL_Texture *glyph = button_glyph( ui, hints[i].button );
        int start = x, h = heights[i] ? heights[i] : TTF_FontHeight( ui->small );

        if (glyph)
        {
            SDL_Rect dst = { x, y - heights[i] / 2, widths[i], heights[i] };
            SDL_RenderCopy( ui->renderer, glyph, NULL, &dst );
            x += widths[i];
        }
        if (hints[i].label && hints[i].label[0])
        {
            if (glyph) x += label_gap;
            ui_text( ui, ui->small, x, y - TTF_FontHeight( ui->small ) / 2, hints[i].label, ui->dim );
            x += ui_text_width( ui, ui->small, hints[i].label );
        }
        if (hints[i].button != UI_NONE)
        {
            ui->footer_hits[ui->footer_count] = (SDL_Rect){ start - 6, y - h / 2 - 8, x - start + 12, h + 16 };
            ui->footer_buttons[ui->footer_count++] = hints[i].button;
        }
        x += hints[i].label && hints[i].label[0] ? pair_gap : glyph_gap;
    }
}

void ui_start_screen( struct ui *ui )
{
    ui->fx_start = SDL_GetTicks();
    ui->highlight = -1;
}

void ui_fade( struct ui *ui )
{
    Uint32 elapsed = SDL_GetTicks() - ui->fx_start;

    if (ui->animations && elapsed < FADE_MS)
        ui_fill( ui, 0, 0, ui->width, ui->height, (SDL_Color){ 0, 0, 0, 200 * (FADE_MS - elapsed) / FADE_MS } );
}

float ui_highlight( struct ui *ui, float target_y )
{
    if (!ui->animations || ui->highlight < 0) ui->highlight = target_y;
    else ui->highlight += (target_y - ui->highlight) * 0.30f;
    if (fabs( ui->highlight - target_y ) < 0.5f) ui->highlight = target_y;
    return ui->highlight;
}

void ui_toast( struct ui *ui, const char *text, int milliseconds )
{
    snprintf( ui->toast, sizeof(ui->toast), "%s", text );
    ui->toast_until = SDL_GetTicks() + milliseconds;
}

void ui_draw_toast( struct ui *ui )
{
    Uint32 now = SDL_GetTicks();
    int w, h, alpha;
    SDL_Color card, text;

    if (!ui->toast[0] || now >= ui->toast_until) return;
    alpha = ui->toast_until - now < 200 ? 255 * (ui->toast_until - now) / 200 : 255;
    /* Between the list panel and the footer. */
    w = ui_text_width( ui, ui->small, ui->toast ) + 40;
    if (w > ui->width - 80) w = ui->width - 80;
    h = TTF_FontHeight( ui->small ) + 12;
    card = ui->selection;
    card.a = 235 * alpha / 255;
    text = (SDL_Color){ 10, 14, 20, alpha };
    ui_rounded( ui, (ui->width - w) / 2, ui->height - 78, w, h, h / 2, card );
    ui_text_fit( ui, ui->small, (ui->width - w) / 2 + 20, ui->height - 78 + 6, w - 40, ui->toast, text, 0 );
}

/***********************************************************************
 * Input and frames
 */

static void queue_event( struct ui *ui, const SDL_Event *event )
{
    if (ui->queued_count < (int)(sizeof(ui->queued) / sizeof(ui->queued[0]))) ui->queued[ui->queued_count++] = *event;
}

static int next_event( struct ui *ui, SDL_Event *event )
{
    if (ui->queued_count)
    {
        *event = ui->queued[0];
        memmove( ui->queued, ui->queued + 1, --ui->queued_count * sizeof(ui->queued[0]) );
        return 1;
    }
    return SDL_PollEvent( event );
}

static void push_button( struct ui *ui, int button )
{
    SDL_Event event;

    memset( &event, 0, sizeof(event) );
    event.type = SDL_CONTROLLERBUTTONDOWN;
    event.cbutton.button = button;
    queue_event( ui, &event );
}

/* Held directions repeat after a delay, from the D-pad or the left stick. */
static void repeat_held( struct ui *ui )
{
    SDL_GameController *c = ui->controller;
    Uint32 now = SDL_GetTicks();
    int direction = 0;

    if (!c) return;
    if (SDL_GameControllerGetButton( c, SDL_CONTROLLER_BUTTON_DPAD_UP ) ||
        SDL_GameControllerGetAxis( c, SDL_CONTROLLER_AXIS_LEFTY ) < -STICK_PRESS) direction = UI_UP;
    else if (SDL_GameControllerGetButton( c, SDL_CONTROLLER_BUTTON_DPAD_DOWN ) ||
             SDL_GameControllerGetAxis( c, SDL_CONTROLLER_AXIS_LEFTY ) > STICK_PRESS) direction = UI_DOWN;
    else if (SDL_GameControllerGetButton( c, SDL_CONTROLLER_BUTTON_DPAD_LEFT ) ||
             SDL_GameControllerGetAxis( c, SDL_CONTROLLER_AXIS_LEFTX ) < -STICK_PRESS) direction = UI_LEFT;
    else if (SDL_GameControllerGetButton( c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT ) ||
             SDL_GameControllerGetAxis( c, SDL_CONTROLLER_AXIS_LEFTX ) > STICK_PRESS) direction = UI_RIGHT;
    if (direction != ui->held)
    {
        ui->held = direction;
        ui->held_since = ui->held_last = now;
        return;
    }
    if (!direction || now - ui->held_since < REPEAT_DELAY_MS || now - ui->held_last < REPEAT_MS) return;
    ui->held_last = now;
    push_button( ui, direction );
}

int ui_begin_frame( struct ui *ui )
{
    if (!ui->running || !platform_running()) return ui->running = 0;
    if (ui->controller && !SDL_GameControllerGetAttached( ui->controller ))
    {
        SDL_GameControllerClose( ui->controller );
        ui->controller = NULL;
        ui->held = ui->stick_x = ui->stick_y = 0;
    }
    ui->scrolling_text = 0;
    repeat_held( ui );
    return 1;
}

static enum ui_touch feed_touch( struct ui *ui, int type, float x, float y, int *steps )
{
    Uint32 now = SDL_GetTicks();
    float dx = x - ui->touch.start_x, dy = y - ui->touch.start_y;

    switch (type)
    {
    case SDL_FINGERDOWN:
        ui->touch.active = 1;
        ui->touch.vertical = 0;
        ui->touch.start_x = x;
        ui->touch.start_y = ui->touch.last_y = y;
        ui->touch.started = now;
        break;
    case SDL_FINGERMOTION:
        if (!ui->touch.active) break;
        if (!ui->touch.vertical && fabs( dy ) > 26 && fabs( dy ) > fabs( dx ) * 1.15f) ui->touch.vertical = 1;
        if (ui->touch.vertical && fabs( y - ui->touch.last_y ) >= 30)
        {
            float step = y - ui->touch.last_y;

            *steps = clampf( fabs( step ) / 30, 1, 6 );
            ui->touch.last_y = y;
            return step < 0 ? UI_TOUCH_SCROLL_UP : UI_TOUCH_SCROLL_DOWN;
        }
        break;
    case SDL_FINGERUP:
        if (!ui->touch.active) break;
        ui->touch.active = 0;
        if (ui->touch.vertical)
        {
            float rest = y - ui->touch.last_y;

            if (fabs( rest ) < 18) break;
            *steps = clampf( fabs( rest ) / 30, 1, 6 );
            return rest < 0 ? UI_TOUCH_SCROLL_UP : UI_TOUCH_SCROLL_DOWN;
        }
        if (fabs( dy ) >= 55 && fabs( dy ) > fabs( dx ) * 1.15f)
        {
            *steps = clampf( fabs( dy ) / 30, 1, 6 );
            return dy < 0 ? UI_TOUCH_SCROLL_UP : UI_TOUCH_SCROLL_DOWN;
        }
        if (fabs( dx ) >= 90 && fabs( dx ) > fabs( dy ) * 1.5f) return dx < 0 ? UI_TOUCH_SWIPE_LEFT : UI_TOUCH_SWIPE_RIGHT;
        if (fabs( dx ) <= 26 && fabs( dy ) <= 26 && now - ui->touch.started <= 400) return UI_TOUCH_TAP;
        break;
    }
    return UI_TOUCH_NONE;
}

static int key_button( SDL_Keycode key )
{
    switch (key)
    {
    case SDLK_UP: return UI_UP;
    case SDLK_DOWN: return UI_DOWN;
    case SDLK_LEFT: return UI_LEFT;
    case SDLK_RIGHT: return UI_RIGHT;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_a: return UI_A;
    case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_b: return UI_B;
    case SDLK_x: return UI_X;
    case SDLK_y: return UI_Y;
    case SDLK_PLUS: case SDLK_EQUALS: case SDLK_KP_PLUS: return UI_PLUS;
    case SDLK_MINUS: case SDLK_KP_MINUS: return UI_MINUS;
    case SDLK_PAGEUP: case SDLK_l: return UI_L;
    case SDLK_PAGEDOWN: case SDLK_r: return UI_R;
    }
    return UI_NONE;
}

int ui_poll( struct ui *ui, struct ui_input *input )
{
    SDL_Event event;

    while (next_event( ui, &event ))
    {
        int type = event.type, i;
        float x = 0, y = 0;

        memset( input, 0, sizeof(*input) );
        input->button = UI_NONE;
        switch (event.type)
        {
        case SDL_QUIT:
            ui->running = 0;
            continue;
        case SDL_CONTROLLERDEVICEADDED:
            if (!ui->controller && SDL_IsGameController( event.cdevice.which ))
                ui->controller = SDL_GameControllerOpen( event.cdevice.which );
            continue;
        case SDL_CONTROLLERBUTTONDOWN:
            input->button = event.cbutton.button;
            break;
        case SDL_CONTROLLERAXISMOTION:
        {
            int *latch = event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ? &ui->stick_x :
                         event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY ? &ui->stick_y : NULL;
            int value = event.caxis.value;

            if (!latch) continue;
            if (value > -STICK_RELEASE && value < STICK_RELEASE) *latch = 0;
            if (*latch || (value > -STICK_PRESS && value < STICK_PRESS)) continue;
            *latch = 1;
            if (latch == &ui->stick_x) input->button = value < 0 ? UI_LEFT : UI_RIGHT;
            else input->button = value < 0 ? UI_UP : UI_DOWN;
            break;
        }
        case SDL_KEYDOWN:
            if ((input->button = key_button( event.key.keysym.sym )) == UI_NONE) continue;
            if (event.key.repeat && input->button != UI_UP && input->button != UI_DOWN &&
                input->button != UI_LEFT && input->button != UI_RIGHT) continue;
            break;
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP:
            x = event.tfinger.x * ui->width;
            y = event.tfinger.y * ui->height;
            break;
        /* A mouse stands in for the touch screen; SDL's own copies of touches are skipped. */
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (event.button.which == SDL_TOUCH_MOUSEID || event.button.button != SDL_BUTTON_LEFT) continue;
            type = event.type == SDL_MOUSEBUTTONDOWN ? SDL_FINGERDOWN : SDL_FINGERUP;
            x = event.button.x;
            y = event.button.y;
            break;
        case SDL_MOUSEMOTION:
            if (event.motion.which == SDL_TOUCH_MOUSEID || !(event.motion.state & SDL_BUTTON_LMASK)) continue;
            type = SDL_FINGERMOTION;
            x = event.motion.x;
            y = event.motion.y;
            break;
        default:
            if (event.type >= SDL_USEREVENT) return 0;  /* a worker has news: redraw */
            continue;
        }
        ui->busy_until = SDL_GetTicks() + 220;
        if (input->button != UI_NONE) return 1;

        input->touch = feed_touch( ui, type, x, y, &input->steps );
        if (input->touch == UI_TOUCH_NONE) continue;
        input->x = x;
        input->y = y;
        if (input->touch == UI_TOUCH_TAP)
            for (i = 0; i < ui->footer_count; i++)
            {
                const SDL_Rect *r = &ui->footer_hits[i];

                if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h)
                {
                    input->touch = UI_TOUCH_NONE;
                    input->button = ui->footer_buttons[i];
                    break;
                }
            }
        return 1;
    }
    return 0;
}

void (*ui_present_hook)( SDL_Renderer *renderer );

void ui_present( struct ui *ui )
{
    ui_draw_toast( ui );
    if (ui_present_hook) ui_present_hook( ui->renderer );
    SDL_RenderPresent( ui->renderer );
}

static int needs_animation( struct ui *ui )
{
    Uint32 now = SDL_GetTicks();
    int moving = ui->highlight >= 0 && ui->last_highlight >= 0 && fabs( ui->highlight - ui->last_highlight ) > 0.2f;

    ui->last_highlight = ui->highlight;
    return ui_animated( ui ) || (ui->animations && now - ui->fx_start < FADE_MS) || moving ||
           ui->scrolling_text || now < ui->busy_until || ui->held || ui->touch.active ||
           (ui->toast[0] && now < ui->toast_until + 50);
}

/* Wait for the next frame: about 60 per second while something moves, else
 * until an event, checking every 250 ms whether the system wants the launcher closed. */
void ui_wait( struct ui *ui )
{
    SDL_Event event;
    Uint32 now = SDL_GetTicks();

    if (ui->queued_count) return;
    if (!needs_animation( ui ))
    {
        ui->deadline = 0;
        for (;;)
        {
            if (SDL_WaitEventTimeout( &event, 250 ))
            {
                queue_event( ui, &event );
                return;
            }
            if (!platform_running())
            {
                ui->running = 0;
                return;
            }
        }
    }
    if (!ui->deadline || SDL_TICKS_PASSED( now, ui->deadline + 16 )) ui->deadline = now;
    ui->deadline += 16;
    if (!SDL_TICKS_PASSED( now, ui->deadline ) && SDL_WaitEventTimeout( &event, ui->deadline - now ))
        queue_event( ui, &event );
}

/***********************************************************************
 * Dialogs and lists
 */

static void draw_card( struct ui *ui, const char *title, const char *heading, const char *text,
                       const struct ui_hint *hints, int hint_count )
{
    const int w = 860, x = (ui->width - w) / 2;
    int lines, h, y;

    ui_background( ui );
    ui_header( ui, title, NULL );
    lines = wrap_text( ui, ui->normal, 0, 0, w - 80, 9, text, ui->text, 0, 0 );
    h = 110 + lines * (TTF_FontHeight( ui->normal ) + 4);
    y = UI_HEADER_HEIGHT + (ui->height - UI_HEADER_HEIGHT - 60 - h) / 2;
    ui_fill( ui, 0, 0, ui->width, ui->height, (SDL_Color){ 0, 0, 0, 90 } );
    ui_rounded( ui, x, y, w, h, 14, ui->card );
    ui_fill( ui, x + 40, y + 70, w - 80, 2, ui->selection );
    ui_text_fit( ui, ui->large, x + 40, y + 18, w - 80, heading, ui->value, 0 );
    ui_text_wrapped( ui, ui->normal, x + 40, y + 88, w - 80, 9, text, ui->text, 0 );
    ui_footer( ui, hints, hint_count );
    ui_fade( ui );
}

static int run_card( struct ui *ui, const char *title, const char *heading, const char *text,
                     const struct ui_hint *hints, int hint_count )
{
    struct ui_input input;

    ui_start_screen( ui );
    while (ui_begin_frame( ui ))
    {
        while (ui_poll( ui, &input ))
        {
            if (input.button == UI_A) return 1;
            if (input.button == UI_B) return 0;
            if (input.touch == UI_TOUCH_TAP && hint_count == 1) return 1;
        }
        draw_card( ui, title, heading, text, hints, hint_count );
        ui_present( ui );
        ui_wait( ui );
    }
    return 0;
}

void ui_message( struct ui *ui, const char *title, const char *text )
{
    static const struct ui_hint hints[] = { { UI_A, "OK" } };

    run_card( ui, title, title, text, hints, 1 );
}

int ui_confirm( struct ui *ui, const char *title, const char *text, const char *yes )
{
    const struct ui_hint hints[] = { { UI_A, yes }, { UI_B, "Cancel" } };

    return run_card( ui, title, title, text, hints, 2 );
}

enum ui_action ui_list_run( struct ui *ui, struct ui_list *list, const char *title, const char *context,
                            const struct ui_row *rows, int count, int can_reset )
{
    const int column_w = 980, column_x = (ui->width - column_w) / 2;
    const int visible = (ui->height - LIST_TOP - 72) / ROW_HEIGHT;
    struct ui_input input;
    int i;

    if (!list->started)
    {
        ui_start_screen( ui );
        list->started = 1;
    }
    while (ui_begin_frame( ui ))
    {
        const struct ui_row *row;
        struct ui_hint hints[6];
        int hint_count = 0, any_adjustable = 0;
        float bar;

        if (count <= 0) return UI_ACTION_BACK;
        if (list->selection >= count) list->selection = count - 1;
        if (list->selection < 0) list->selection = 0;
        while (ui_poll( ui, &input ))
        {
            int direction = 0;

            row = rows + list->selection;
            switch (input.touch)
            {
            case UI_TOUCH_SCROLL_UP:
            case UI_TOUCH_SCROLL_DOWN:
                list->selection += (input.touch == UI_TOUCH_SCROLL_UP ? 1 : -1) * input.steps;
                if (list->selection >= count) list->selection = count - 1;
                if (list->selection < 0) list->selection = 0;
                continue;
            case UI_TOUCH_SWIPE_LEFT:
            case UI_TOUCH_SWIPE_RIGHT:
                if (!row->disabled && row->adjustable)
                    return input.touch == UI_TOUCH_SWIPE_LEFT ? UI_ACTION_LEFT : UI_ACTION_RIGHT;
                continue;
            case UI_TOUCH_TAP:
            {
                int index = list->top + (input.y - LIST_TOP) / ROW_HEIGHT;

                if (input.y < UI_HEADER_HEIGHT) return UI_ACTION_BACK;
                if (input.x < column_x || input.x >= column_x + column_w || input.y < LIST_TOP ||
                    index >= count || index >= list->top + visible) continue;
                list->selection = index;
                if (rows[index].disabled) continue;
                if (rows[index].adjustable) return input.x >= column_x + column_w / 2 ? UI_ACTION_RIGHT : UI_ACTION_LEFT;
                return UI_ACTION_CHOOSE;
            }
            default:
                break;
            }
            switch (input.button)
            {
            case UI_UP: direction = -1; break;
            case UI_DOWN: direction = 1; break;
            case UI_L: list->selection = list->selection - visible < 0 ? 0 : list->selection - visible; break;
            case UI_R: list->selection = list->selection + visible >= count ? count - 1 : list->selection + visible; break;
            case UI_LEFT: if (!row->disabled && row->adjustable) return UI_ACTION_LEFT; break;
            case UI_RIGHT: if (!row->disabled && row->adjustable) return UI_ACTION_RIGHT; break;
            case UI_A: if (!row->disabled) return UI_ACTION_CHOOSE; break;
            case UI_B: return UI_ACTION_BACK;
            case UI_Y: if (can_reset && !row->disabled && row->adjustable) return UI_ACTION_RESET; break;
            case UI_X:
                if (row->help)
                {
                    ui_message( ui, row->label, row->help );
                    ui_start_screen( ui );
                }
                break;
            }
            if (direction)
            {
                int next = list->selection;

                do next = (next + direction + count) % count;
                while (rows[next].disabled && next != list->selection);
                list->selection = next;
            }
        }
        if (!ui->running) break;

        if (list->selection < list->top) list->top = list->selection;
        if (list->selection >= list->top + visible) list->top = list->selection - visible + 1;
        if (list->top > count - visible) list->top = count - visible;
        if (list->top < 0) list->top = 0;
        row = rows + list->selection;

        ui_background( ui );
        ui_header( ui, title, context );
        ui_panel( ui, column_x - 12, LIST_TOP - 10, column_w + 24, visible * ROW_HEIGHT + 18 );
        bar = ui_highlight( ui, LIST_TOP + (list->selection - list->top) * ROW_HEIGHT + 1 );
        ui_fill( ui, column_x, bar, column_w, ROW_HEIGHT - 2, ui->focus );
        ui_fill( ui, column_x, bar, 5, ROW_HEIGHT - 2, ui->selection );
        for (i = list->top; i < count && i < list->top + visible; i++)
        {
            int y = LIST_TOP + (i - list->top) * ROW_HEIGHT, current = i == list->selection;
            int text_y = y + (ROW_HEIGHT - TTF_FontHeight( ui->normal )) / 2;
            int value_w = rows[i].value[0] ? ui_text_width( ui, ui->small, rows[i].value ) : 0;
            int label_w = column_w - 80 - (value_w ? (value_w < column_w / 3 ? value_w : column_w / 3) + 24 : 0);
            SDL_Color color = rows[i].disabled ? ui->dim : rows[i].destructive ? ui->danger : current ? ui->value : ui->text;

            any_adjustable |= rows[i].adjustable && !rows[i].disabled;
            ui_text_fit( ui, ui->normal, column_x + 40, text_y, label_w, rows[i].label, color, current );
            if (value_w)
                ui_text_fit( ui, ui->small, column_x + column_w - 40 - (value_w < column_w / 3 ? value_w : column_w / 3),
                             text_y + (TTF_FontHeight( ui->normal ) - TTF_FontHeight( ui->small )) / 2,
                             column_w / 3, rows[i].value, current ? ui->value : ui->dim, current );
        }
        if (count > visible)
        {
            int track_h = visible * ROW_HEIGHT, thumb = track_h * visible / count;

            if (thumb < 16) thumb = 16;
            ui_fill( ui, column_x + column_w + 16, LIST_TOP - 2, 4, track_h, (SDL_Color){ 40, 44, 54, 255 } );
            ui_fill( ui, column_x + column_w + 16, LIST_TOP - 2 + (track_h - thumb) * list->top / (count - visible),
                     4, thumb, ui->selection );
        }
        if (any_adjustable) hints[hint_count++] = (struct ui_hint){ UI_LEFT, NULL };
        if (any_adjustable) hints[hint_count++] = (struct ui_hint){ UI_RIGHT, "Change" };
        if (!row->disabled) hints[hint_count++] = (struct ui_hint){ UI_A, row->adjustable ? "Next" : "Choose" };
        if (row->help) hints[hint_count++] = (struct ui_hint){ UI_X, "Info" };
        if (can_reset && row->adjustable && !row->disabled) hints[hint_count++] = (struct ui_hint){ UI_Y, "Default" };
        hints[hint_count++] = (struct ui_hint){ UI_B, "Back" };
        ui_footer( ui, hints, hint_count );
        ui_fade( ui );
        ui_present( ui );
        ui_wait( ui );
    }
    return UI_ACTION_QUIT;
}
