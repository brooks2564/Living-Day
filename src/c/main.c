// ── Living Day  ·  main.c ─────────────────────────────────────────────────
// A biometric landscape watchface. The world starts dead and comes to life
// as your step count grows throughout the day.
// Built for Pebble Round 2 (gabbro). Runs on all 7 platforms.
#include <pebble.h>

static Window            *s_window;
static Layer             *s_canvas;

static char               s_time_buf[6];   // "HH:MM"
static char               s_date_buf[16];  // "Thu Apr 10"
static int                s_hour    = 12;
static int                s_daily_steps = 0;
static BatteryChargeState s_battery;

// ── Fixed star positions (within 180×180 round screen, sky area y<130) ──
static const GPoint STARS[] = {
  {25,30},{55,18},{88,12},{118,22},{150,28},
  {18,58},{50,44},{96,38},{140,46},{163,40},
  {30,82},{74,68},{112,72},{152,66},{138,90}
};
#define NUM_STARS 15

// ── Sky color based on hour ───────────────────────────────────────────────
static GColor sky_color(int hour) {
  if (hour >= 9  && hour < 17) return GColorVividCerulean;
  if ((hour >= 7  && hour < 9) ||
      (hour >= 17 && hour < 19)) return GColorOrange;
  if (hour >= 5  && hour < 7)  return GColorImperialPurple;
  return GColorOxfordBlue;
}

// ── Ground / mountain color based on step % ───────────────────────────────
static GColor vitality_color(int pct) {
  if (pct >= 75) return GColorIslamicGreen;
  if (pct >= 50) return GColorLimerick;
  if (pct >= 25) return GColorBrass;
  return GColorDarkGray;
}

// ── Draw one mountain (filled triangle via GPath) ─────────────────────────
static void draw_mountain(GContext *ctx, int peak_x, int peak_y,
                          int base_y, int half_w) {
  GPoint pts[3] = {
    {peak_x - half_w, base_y},
    {peak_x,          peak_y},
    {peak_x + half_w, base_y}
  };
  GPathInfo info = {.num_points = 3, .points = pts};
  GPath *path = gpath_create(&info);
  if (path) { gpath_draw_filled(ctx, path); gpath_destroy(path); }
}

// ── Draw a tree (dead branches or growing foliage) ────────────────────────
static void draw_tree(GContext *ctx, int x, int ground_y, int pct) {
  int trunk_h = 24;

  // Trunk
#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorBrass);
#else
  graphics_context_set_fill_color(ctx, GColorWhite);
#endif
  graphics_fill_rect(ctx, GRect(x - 2, ground_y - trunk_h, 4, trunk_h),
                     0, GCornerNone);

  if (pct < 15) {
    // Dead — bare branches
#ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
#else
    graphics_context_set_stroke_color(ctx, GColorLightGray);
#endif
    int bx = x, by = ground_y - trunk_h;
    graphics_draw_line(ctx, GPoint(bx, by),   GPoint(bx-13, by-10));
    graphics_draw_line(ctx, GPoint(bx, by),   GPoint(bx+11, by - 8));
    graphics_draw_line(ctx, GPoint(bx, by+7), GPoint(bx - 8, by  ));
    graphics_draw_line(ctx, GPoint(bx, by+7), GPoint(bx + 7, by+1));
  } else {
    // Alive — foliage circle grows with steps (r: 5→18)
    int r = 5 + (pct * 13) / 100;
    if (r > 18) r = 18;
#ifdef PBL_COLOR
    GColor fc = pct >= 60 ? GColorIslamicGreen : GColorLimerick;
    graphics_context_set_fill_color(ctx, fc);
#else
    graphics_context_set_fill_color(ctx, GColorWhite);
#endif
    graphics_fill_circle(ctx, GPoint(x, ground_y - trunk_h - r / 2 - 2), r);
  }
}

// ── Draw a flower (stem + bloom) ─────────────────────────────────────────
static void draw_flower(GContext *ctx, int x, int ground_y, GColor color) {
#ifdef PBL_COLOR
  graphics_context_set_stroke_color(ctx, GColorIslamicGreen);
  graphics_draw_line(ctx, GPoint(x, ground_y - 1), GPoint(x, ground_y - 10));
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, GPoint(x, ground_y - 14), 4);
#else
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(x, ground_y - 14), 3);
#endif
}

// ── Main canvas draw ──────────────────────────────────────────────────────
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int w = bounds.size.w, h = bounds.size.h, cx = w / 2;
  int step_pct = s_daily_steps >= 10000 ? 100
               : (s_daily_steps * 100) / 10000;
  int horizon_y = h * 72 / 100;  // ~130px on 180px, ~164px on 228px

  // ── 1. Sky ──────────────────────────────────────────────────────────────
#ifdef PBL_COLOR
  GColor sky;
  if (step_pct < 15)
    sky = GColorDarkGray;           // dead world = overcast grey
  else
    sky = sky_color(s_hour);
  graphics_context_set_fill_color(ctx, sky);
#else
  graphics_context_set_fill_color(ctx, GColorBlack);
#endif
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // ── 2. Stars (night or dead/overcast world) ──────────────────────────
  bool is_night = (s_hour >= 19 || s_hour < 6);
  if (is_night || step_pct < 15) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    for (int i = 0; i < NUM_STARS; i++) {
      int sx = STARS[i].x * w / 180;   // scale to screen width
      int sy = STARS[i].y * h / 180;
      if (sy < horizon_y - 4)
        graphics_fill_rect(ctx, GRect(sx, sy, 2, 2), 0, GCornerNone);
    }
  }

  // ── 3. Sun / Moon (only in a living world) ───────────────────────────
  if (step_pct >= 15) {
    int arc_r  = w * 36 / 100;   // radius of arc scales to screen
    bool sun_up  = (s_hour >= 6  && s_hour < 18);
    bool moon_up = (s_hour >= 18 || s_hour < 6);

    if (sun_up) {
      // 6am=180° → noon=90° → 6pm=0°
      int mins     = (s_hour - 6) * 60;
      int ang_deg  = 180 - (mins * 180) / 720;
      int32_t a    = (int32_t)((ang_deg + 360) % 360) * TRIG_MAX_ANGLE / 360;
      int sx = cx + (int)((cos_lookup(a) * arc_r) / TRIG_MAX_RATIO);
      int sy = horizon_y - (int)((sin_lookup(a) * arc_r) / TRIG_MAX_RATIO);
#ifdef PBL_COLOR
      graphics_context_set_fill_color(ctx, GColorYellow);
#else
      graphics_context_set_fill_color(ctx, GColorWhite);
#endif
      graphics_fill_circle(ctx, GPoint(sx, sy), 9);
    }

    if (moon_up) {
      // 6pm=180° → midnight=90° → 6am=0°
      int h_norm   = s_hour >= 18 ? s_hour - 18 : s_hour + 6;
      int mins     = h_norm * 60;
      int ang_deg  = 180 - (mins * 180) / 720;
      int32_t a    = (int32_t)((ang_deg + 360) % 360) * TRIG_MAX_ANGLE / 360;
      int mx = cx + (int)((cos_lookup(a) * arc_r) / TRIG_MAX_RATIO);
      int my = horizon_y - (int)((sin_lookup(a) * arc_r) / TRIG_MAX_RATIO);
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_circle(ctx, GPoint(mx, my), 7);
    }
  }

  // ── 4. Mountains (appear at 20%+, grow to 60px at 100%) ─────────────
  if (step_pct >= 20) {
    int max_h = (step_pct * 60) / 100;
#ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, vitality_color(step_pct - 10));
#else
    graphics_context_set_fill_color(ctx, GColorDarkGray);
#endif
    // Background peaks (shorter)
    draw_mountain(ctx, cx - 42, horizon_y - max_h * 7/10, horizon_y, 44);
    draw_mountain(ctx, cx + 52, horizon_y - max_h * 8/10, horizon_y, 38);
#ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, vitality_color(step_pct));
#endif
    // Foreground centre peak (tallest)
    draw_mountain(ctx, cx, horizon_y - max_h, horizon_y, 54);
  }

  // ── 5. Ground ────────────────────────────────────────────────────────
#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, vitality_color(step_pct));
#else
  graphics_context_set_fill_color(ctx,
    step_pct > 30 ? GColorLightGray : GColorDarkGray);
#endif
  graphics_fill_rect(ctx, GRect(0, horizon_y, w, h - horizon_y), 0, GCornerNone);

  // ── 6. Trees ─────────────────────────────────────────────────────────
  // Two side trees always present (dead or alive)
  draw_tree(ctx, w * 16/100, horizon_y, step_pct);
  draw_tree(ctx, w * 84/100, horizon_y, step_pct);
  // Third tree appears at 40%
  if (step_pct >= 40)
    draw_tree(ctx, cx - 52, horizon_y, step_pct);
  // Fourth tree appears at 65%
  if (step_pct >= 65)
    draw_tree(ctx, cx + 48, horizon_y, step_pct);

  // ── 7. Flowers (progressive) ─────────────────────────────────────────
#ifdef PBL_COLOR
  if (step_pct >= 30)
    draw_flower(ctx, w * 27/100, horizon_y, GColorRed);
  if (step_pct >= 45)
    draw_flower(ctx, cx + 32,    horizon_y, GColorYellow);
  if (step_pct >= 60) {
    draw_flower(ctx, cx - 22,    horizon_y, GColorMagenta);
    draw_flower(ctx, w * 73/100, horizon_y, GColorRed);
  }
  if (step_pct >= 75) {
    draw_flower(ctx, cx + 58,    horizon_y, GColorYellow);
    draw_flower(ctx, w * 40/100, horizon_y, GColorCyan);
  }
  if (step_pct >= 90) {
    draw_flower(ctx, w * 20/100, horizon_y, GColorMagenta);
    draw_flower(ctx, w * 62/100, horizon_y, GColorYellow);
  }
#else
  // B&W: simple white dots at same positions
  if (step_pct >= 30) draw_flower(ctx, w*27/100, horizon_y, GColorWhite);
  if (step_pct >= 45) draw_flower(ctx, cx+32,    horizon_y, GColorWhite);
  if (step_pct >= 60) draw_flower(ctx, cx-22,    horizon_y, GColorWhite);
#endif

  // ── 8. Time ──────────────────────────────────────────────────────────
  GFont big = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_time_buf, big,
    GRect(0, h * 9/100, w, 48),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  // ── 9. Date ──────────────────────────────────────────────────────────
  GFont sml = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  graphics_context_set_text_color(ctx,
    step_pct < 15 ? GColorDarkGray : GColorLightGray);
  graphics_draw_text(ctx, s_date_buf, sml,
    GRect(0, h * 9/100 + 50, w, 22),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  // ── 10. Battery bar ───────────────────────────────────────────────────
#ifdef PBL_COLOR
  GColor bc = s_battery.charge_percent > 50 ? GColorGreen :
              s_battery.charge_percent > 20 ? GColorYellow : GColorRed;
#else
  GColor bc = GColorWhite;
#endif
#ifdef PBL_ROUND
  // Curved arc along bottom of round screen (135° → 225°, clockwise through 6 o'clock)
  int32_t arc_start = DEG_TO_TRIGANGLE(135);
  int32_t arc_end   = DEG_TO_TRIGANGLE(225);
  int32_t arc_fill  = arc_start + (arc_end - arc_start) * s_battery.charge_percent / 100;
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_radial(ctx, bounds, GOvalScaleModeFitCircle, 4, arc_start, arc_end);
  graphics_context_set_fill_color(ctx, bc);
  if (s_battery.charge_percent > 0)
    graphics_fill_radial(ctx, bounds, GOvalScaleModeFitCircle, 4, arc_start, arc_fill);
#else
  int bw = (s_battery.charge_percent * w) / 100;
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, GRect(0, h - 5, w, 3), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, bc);
  graphics_fill_rect(ctx, GRect(0, h - 5, bw, 3), 0, GCornerNone);
#endif
}

// ── Handlers ──────────────────────────────────────────────────────────────
static void update_time(struct tm *t) {
  strftime(s_time_buf, sizeof(s_time_buf), "%H:%M", t);
  strftime(s_date_buf, sizeof(s_date_buf), "%a %b %d", t);
  s_hour = t->tm_hour;
}

static void tick_handler(struct tm *t, TimeUnits u) {
  update_time(t);
  layer_mark_dirty(s_canvas);
}

#ifdef PBL_HEALTH
static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventSignificantUpdate ||
      event == HealthEventMovementUpdate) {
    HealthValue v = health_service_sum_today(HealthMetricStepCount);
    s_daily_steps = (int)v;
    layer_mark_dirty(s_canvas);
  }
}
#endif

static void battery_handler(BatteryChargeState state) {
  s_battery = state;
  layer_mark_dirty(s_canvas);
}

// ── Window ────────────────────────────────────────────────────────────────
static void window_load(Window *window) {
  Layer *root  = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);
  s_canvas = layer_create(bounds);
  layer_set_update_proc(s_canvas, canvas_update_proc);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
}

// ── Init / Deinit ─────────────────────────────────────────────────────────
static void init(void) {
  time_t now = time(NULL);
  update_time(localtime(&now));

#ifdef PBL_HEALTH
  s_daily_steps = (int)health_service_sum_today(HealthMetricStepCount);
#endif
  s_battery = battery_state_service_peek();

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
#ifdef PBL_HEALTH
  health_service_events_subscribe(health_handler, NULL);
#endif
  battery_state_service_subscribe(battery_handler);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
#ifdef PBL_HEALTH
  health_service_events_unsubscribe();
#endif
  battery_state_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
