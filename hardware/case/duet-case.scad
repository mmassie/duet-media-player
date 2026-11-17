// =====================================================================
//  Duet case (Folio design) — Inkplate 6 e-reader + MP3 player
//  Parametric OpenSCAD model. Every dimension is in mm.
//
//  Export: run tools/export-case.sh from the repo root (all parts, or one:
//  tools/export-case.sh bezel). By hand, one part at a time:
//    openscad -D 'part="bezel"' -o stl/duet-bezel.stl duet-case.scad
//    parts: bezel, tray, keys (keys + battery strap), fit_test
//    part="assembly" shows everything (preview only, not for printing)
//
//  Coordinates: X = across the device, Y = up the device (0 = bottom),
//  Z = thickness (0 = front face of the screen bezel, grows toward the back).
//  Note: you look at the screen from -Z, so +X runs toward YOUR LEFT. A part at
//  small X is near the right-hand edge when you hold the reader.
//  Board positions come from Soldered's Inkplate 6 KiCad file (v1.2.1) and
//  Adafruit's Eagle files for the Feather, UDA1334A and #4682 microSD boards.
//
//  The three audio boards mount FACE-DOWN on M2.5 heat-set insert standoffs
//  that rise from the inside of the back: components toward the back wall,
//  flat side toward the Inkplate. The battery sits in a printed cradle under a
//  screwed-down strap. No tape or glue holds anything.
// =====================================================================

include <inkplate_parts.scad>

part = "assembly";   // bezel | tray | keys | fit_test | key_test | assembly

/* [Inkplate 6 board] */
board_w     = 107.8;   // board width  (short side)
board_h     = 144.4;   // board length (long side)
active_w    = 90.6;    // ED060SC7 active area, portrait
active_h    = 122.4;
panel_gap   = 0.4;     // foam gasket between bezel and panel
stack       = 10.0;    // panel front -> back of the board's M3 standoffs. MEASURE THIS.
pcb_back    = 3.0;     // panel front -> back face of the PCB
window_dy   = 5.0;     // screen window sits this far toward the top of the case, off the board centre

/* [Case] */
wall        = 2.0;
clear       = 0.3;     // gap around the board
plate       = 1.6;     // bezel thickness over the screen
chin        = 16;      // space below the board for the music buttons
back        = 1.6;     // back wall thickness
cavity      = 15.0;    // front face -> inside of back wall
corner_r    = 4;
edge_chamfer= 1.0;
skirt_d     = 2.0;     // depth of the bezel's outer lip
fit         = 0.15;    // sliding fit clearance for the lip joint

/* [Top edge: tongue-and-groove] */
// The Inkplate fills the case edge to edge, so nothing can screw down the top of
// the bezel. Instead the tray's top wall comes forward to the front face, and two
// tongues on the bezel's top edge slide into slots in it. Fit: set the bezel down
// about 1.5 mm low, slide it up until the tongues seat, then the two chin screws
// stop it sliding back, so the tongues can't come out.
top_band    = wall;    // height of the tray's top wall where it reaches the front face
tongue_w    = 24;      // width of each tongue
tongue_len  = 1.2;     // how far each tongue reaches into the top wall
tongue_z0   = 0.6;     // front of the tongue (plastic in front of it holds the bezel down)
tongue_z1   = 1.8;     // back of the tongue
tongue_fit  = 0.1;     // clearance around the tongue in its slot (Z and X)
tongue_end  = 0.2;     // spare depth at the end of each slot

/* [Fasteners] */
m3_clear    = 3.4;
m3_head     = 6.2;
m2_clear    = 2.4;
m2_head     = 4.2;
insert_d    = 3.2;     // M2 heat-set insert hole
insert_len  = 4.0;

/* [M2.5 heat-set inserts (audio boards + battery strap)] */
ins25_d     = 3.6;     // hole for your insert: check its datasheet (3.5-4.0 typical)
ins25_len   = 4.0;     // insert length
ins25_od    = 6.0;     // standoff outer diameter
m25_clear   = 2.9;
m25_head_d  = 5.2;     // button-head counterbore in the strap
m25_head_h  = 1.4;

/* [Audio boards] all heights in mm */
pcb_t       = 1.6;
feather_tall = 4.8;    // tallest part on the Feather (its battery JST)
dac_tall    = 5.5;     // headphone jack height. MEASURE (5-6 typical)
dac_jack_axis = 3.0;   // jack centre above the DAC's PCB
sd_tall     = 1.9;     // microSD socket
gap_board   = 0.4;     // between a board's tallest part and the back wall
min_floor   = 0.6;     // plastic left under each insert hole

/* [Battery] */
batt_w      = 40;      // across the device
batt_l      = 80;      // along the device
batt_t      = 5.0;     // thickness; the strap clamps to exactly this
batt_xoff   = 58;      // mm in from the Inkplate's right-hand edge (as held)
batt_ytop   = 36;      // mm down from the Inkplate's USB edge
strap_ytop  = 74;      // strap centre line
strap_w     = 8;
strap_t     = 2.4;

/* [Keys] */
key_hole    = 7.0;
key_d       = 6.4;
key_flange  = 8.6;      // square flange: it keeps the key (and its icon) from turning
key_flange_t= 1.0;      // flange thickness
key_slack   = 0.3;      // free play between a key's flange and its switch's plunger
key_side_fit= 0.3;      // gap each side of a chin key's flange, between the bezel rails
icon_depth  = 0.5;      // how deep the icons are sunk into the key tops (0 = plain keys)

/* [Tactile switches: 6 x 6 x 5 mm through-hole] */
tact_body   = 6.0;      // body, square
tact_body_h = 3.5;      // body height without the plunger
tact_h      = 5.0;      // height including the plunger
tact_fit    = 0.25;     // clearance each side of the body in its pocket
tact_leg_pitch = 6.5;   // centre distance between the two rows of legs
tact_leg_len   = 3.5;   // how far the legs reach past the base

$fn = 48;

// ---------- derived ----------
T  = cavity + back;                         // total thickness
W  = board_w + 2*clear + 2*wall;
H  = wall + clear + board_h + clear + chin + wall;
bx0 = wall + clear;                         // board left edge (X)
by_top = H - wall - clear;                  // board top edge (Y)
z_panel = plate + panel_gap;                // panel front face
z_pcb_back = z_panel + pcb_back;
z_stand = z_panel + stack;                  // where the board's standoffs end

// standoff heights (inside of the back -> component face of each board)
s_feather = feather_tall + gap_board;       // 5.2
s_dac     = dac_tall + gap_board;           // 5.9
s_sd      = max(sd_tall + gap_board, ins25_len + 0.6 + min_floor - back);  // 3.6, set by the insert
s_strap   = batt_t;                         // strap face rests on the battery
function zface(s) = cavity - s;             // z of a board's component face

// board coordinates (X from left, Y down from the USB edge) -> model
function bp(x, ytop) = [bx0 + x, by_top - ytop];

// Inkplate edge parts. KiCad gives each part's distance along the short edge
// (its y). With the component side facing the back and the USB edge at the
// top, the model X of a part is board_w - y.
function from_kicad(y) = board_w - y;
usb_x    = from_kicad(92.8);  usb_z = z_pcb_back + 1.6;   // USB-C       (15 mm in from the right edge, held)
sd_x     = from_kicad(63.8);  sd_z  = z_pcb_back + 1.0;   // book microSD (44 mm from the right)
pwr_x    = from_kicad(81.8);  pwr_z = z_pcb_back + 1.75;  // power pin   (26 mm from the right)
wake_x   = from_kicad(81.8);  wake_z= z_pcb_back + 1.75;  // WAKE plunger, bottom edge (26 mm from the right)
holes    = [[3.4,3.4],[104.4,3.4],[3.4,141.0],[104.4,141.0]];  // board M3 standoffs

// ---------- audio board layout ----------
// Each board is placed by the model coordinates of its local (0,0) corner and a
// mapping from its own PCB coordinates (Adafruit's Eagle files, mm). Seen from the
// BACK with the component side toward you, model +X is to the right, so these are
// plain rotations, never mirror images. All three sit along the X = 0 wall, which is
// the RIGHT side when you hold the reader. Positions were chosen to clear the
// Inkplate's taller parts (see inkplate_parts.scad).
//
//   HUZZAH32 Feather  50.8 x 22.86   USB edge (lx = 0) at the wall
//   UDA1334A DAC      38.1 x 25.4    jack edge (lx = 38.1) at the wall, rotated 180
//   #4682 microSD     25.4 x 22.86   card edge (ly = 22.86) at the wall, rotated 90
feather_ytop = 104;    dac_ytop = 76;    sd_ytop = 20;      // top edge of each board
feather_o = [wall + 0.6, by_top - feather_ytop - 22.86];
dac_o     = [wall + 0.3 + 2.5, by_top - dac_ytop - 25.4];   // jack mouth 0.3 mm inside the wall
sd_o      = [wall + 0.6, by_top - sd_ytop - 25.4];
function feather_xy(l) = feather_o + [l[0], l[1]];
function dac_xy(l)     = dac_o + [38.1 - l[0], 25.4 - l[1]];
function sd_xy(l)      = sd_o + [22.86 - l[1], l[0]];
feather_m25  = [[2.54, 2.54], [2.54, 20.32]];             // plated 2.5 mm holes (USB end)
feather_pins = [[48.26, 1.84], [48.26, 20.95]];           // 2.2 mm holes: printed locating pins
dac_m25      = [[2.54, 2.54], [35.56, 2.54], [2.54, 22.86], [35.56, 22.86]];
sd_m25       = [[2.54, 20.32], [22.86, 20.32]];

// connector centres on the X = 0 wall
feather_usb = feather_xy([0, 11.43]);  feather_usb_z = zface(s_feather) + 1.5;
dac_jack    = dac_xy([38.1, 12.7]);    jack_z        = zface(s_dac) + dac_jack_axis;
sd_card     = sd_xy([12.64, 22.86]);   sd_card_z     = zface(s_sd) + 0.9;

// side page keys (both walls) and their switch pockets
page_y      = 64;
key_z       = 10.5;
slide_x     = 88;                                  // audio power switch, bottom edge (left side, held)
slide_z     = 10.0;
chin_keys_x = [38.0, 53.9, 69.8];                  // ◀◀ ▶‖ ▶▶ (centre one under the screen centre)
chin_key_y  = wall + chin/2;
boss_x      = [8, W - 8];                        // bezel screws (chin)
boss_y      = wall + chin/2;
y_seam      = H - top_band;                      // front-face seam between bezel and tray
tongue_x    = [W/2 - 28, W/2 + 28];              // tongue centres, clear of the USB-C relief

// ---------- helpers ----------
module rrect(w, h, r, z) {                      // rounded rectangle slab
  translate([r, r, 0]) linear_extrude(z) offset(r) square([w - 2*r, h - 2*r]);
}
module body(w, h, r, z0, z1, ch) {              // rounded box with chamfered faces
  hull() {
    translate([ch, ch, z0]) rrect(w - 2*ch, h - 2*ch, max(r - ch, 0.5), 0.01);
    translate([0, 0, z0 + ch]) rrect(w, h, r, z1 - z0 - 2*ch);
    translate([ch, ch, z1 - 0.01]) rrect(w - 2*ch, h - 2*ch, max(r - ch, 0.5), 0.01);
  }
}
module slot_x(x, y, z, w, h, depth) {           // slot through a wall facing ±Y
  translate([x, y, z]) rotate([90,0,0]) linear_extrude(depth, center=true)
    offset(min(h/2 - 0.01, 1.2)) offset(-min(h/2 - 0.01, 1.2)) square([w, h], center=true);
}
module slot_y(x, y, z, w, h, depth) {           // slot through a wall facing ±X
  translate([x, y, z]) rotate([0,90,0]) linear_extrude(depth, center=true)
    offset(min(w/2 - 0.01, 1.2)) offset(-min(w/2 - 0.01, 1.2)) square([h, w], center=true);
}

// =====================================================================
//  BEZEL: front plate + outer lip + screw bosses in the chin
// =====================================================================
module bezel() {
  wc = bp(board_w/2, board_h/2) + [0, window_dy];   // screen centre: the active area sits above the board centre
  union() {
    intersection() {               // stops short of the tray's top wall
      bezel_body(wc);
      translate([-1, -1, -1]) cube([W + 2, y_seam - fit + 1, T + 2]);
    }
    for (x = tongue_x) translate([x - tongue_w/2, y_seam - fit - 0.01, tongue_z0])
      cube([tongue_w, fit + tongue_len + 0.01, tongue_z1 - tongue_z0]);
  }
}
module bezel_body(wc) {
  difference() {
    union() {
      // front plate
      intersection() {
        body(W, H, corner_r, 0, T, edge_chamfer);
        translate([-1, -1, -1]) cube([W + 2, H + 2, plate + 1]);
      }
      // outer lip that wraps over the tray's stepped wall
      intersection() {
        body(W, H, corner_r, 0, T, edge_chamfer);
        difference() {
          translate([-1, -1, plate - 0.01]) cube([W + 2, H + 2, skirt_d + 0.01]);
          translate([wall/2 - fit, wall/2 - fit, plate - 1])
            rrect(W - wall + 2*fit, H - wall + 2*fit, corner_r - wall/2, skirt_d + 2);
          // no lip round the top corners: the bezel has to slide up past them
          translate([-1, H - corner_r - fit, plate - 1]) cube([W + 2, corner_r + 2, skirt_d + 2]);
        }
      }
      // two low rails either side of the chin keys: their square flanges slide in
      // between and can't turn, so the icons stay upright
      for (sy = [-1, 1]) translate([bx0 + chin_keys_x[0] - 5.5, chin_key_y + (sy > 0 ? key_flange/2 + key_side_fit : -key_flange/2 - key_side_fit - 0.8), plate - 0.01])
        cube([chin_keys_x[2] - chin_keys_x[0] + 11, 0.8, key_flange_t + 0.01]);
      // M2 bosses in the chin
      for (x = boss_x) translate([x, boss_y, plate - 0.01])
        cylinder(d = 6.5, h = cavity - plate - 0.2);
    }
    // screen window, chamfered toward the reader
    translate([wc[0], wc[1], 0]) hull() {
      translate([0, 0, plate - 0.01]) cube([active_w + 1.0, active_h + 1.0, 0.02], center=true);
      translate([0, 0, -0.01]) cube([active_w + 4.4, active_h + 4.4, 0.02], center=true);
    }
    // chin keys
    for (x = chin_keys_x) translate([bx0 + x, chin_key_y, -1]) cylinder(d = key_hole, h = plate + 2);
    // heat-set insert holes
    for (x = boss_x) translate([x, boss_y, cavity - 0.2 - insert_len]) cylinder(d = insert_d, h = insert_len + 1);
  }
}

// =====================================================================
//  TRAY: back + walls, board posts, port cutouts, key holes, wake plunger guide
// =====================================================================
module tray() {
  difference() {
    union() {
      difference() {
        intersection() {
          body(W, H, corner_r, 0, T, edge_chamfer);
          translate([-1, -1, plate]) cube([W + 2, H + 2, T]);
        }
        // hollow inside
        translate([wall, wall, -1]) rrect(W - 2*wall, H - 2*wall, 1.0, cavity + 1);  // tight corners: the PCB is square
        // step for the bezel lip: remove the outer half of the wall near the front
        difference() {
          translate([-1, -1, -1]) cube([W + 2, H + 2, plate + skirt_d + 1]);
          translate([wall/2, wall/2, -2]) rrect(W - wall, H - wall, corner_r - wall/2, plate + skirt_d + 4);
          translate([-1, H - corner_r, -2]) cube([W + 2, corner_r + 2, plate + skirt_d + 4]);  // full wall at the top corners
        }
      }
      // top wall comes forward to the front face (the bezel's tongues slot into it)
      intersection() {
        body(W, H, corner_r, 0, T, edge_chamfer);
        translate([-1, y_seam, -1]) cube([W + 2, top_band + 1, plate + skirt_d + 1.01]);
      }
      // posts that meet the Inkplate's M3 standoffs
      for (h = holes) { p = bp(h[0], h[1]);
        translate([p[0], p[1], z_stand]) cylinder(d = 7, h = cavity - z_stand + 0.01); }
      // guide tube for the WAKE plunger, tied to the back wall by a fin
      wk = bp(wake_x, board_h);
      translate([wk[0] - 3, wall - 0.01, wake_z - 3]) cube([6, wk[1] - wall - 3, 6]);
      translate([wk[0] - 1, wall - 0.01, wake_z]) cube([2, wk[1] - wall - 3, cavity - wake_z + 0.01]);
      // a seat behind each chin key: its switch sits on it, plunger toward the key
      for (x = chin_keys_x) chin_seat(bx0 + x);
      // ---- audio board standoffs ----
      for (l = feather_m25) standoff(feather_xy(l), s_feather, ins25_od);
      for (l = feather_pins) let(p = feather_xy(l)) {
        standoff(p, s_feather, 4.2);
        translate([p[0], p[1], zface(s_feather) - pcb_t + 0.3]) cylinder(d = 1.8, h = pcb_t - 0.3 + 0.01);
      }
      for (l = dac_m25) standoff(dac_xy(l), s_dac, ins25_od);
      for (l = sd_m25)  standoff(sd_xy(l), s_sd, 5.6);
      // ---- battery cradle: four corner guides ----
      battery_cradle();
      // ---- strap standoffs either side of the battery ----
      for (p = strap_posts()) standoff(p, s_strap, ins25_od);
      // ---- page-key switch pockets (both walls) ----
      key_pocket();
      translate([W, 0, 0]) mirror([1, 0, 0]) key_pocket();
      // ---- slide switch pocket (bottom wall) ----
      slide_pocket();
    }
    // ---- slots for the bezel's tongues ----
    for (x = tongue_x) translate([x - tongue_w/2 - tongue_fit, y_seam - 0.01, tongue_z0 - tongue_fit])
      cube([tongue_w + 2*tongue_fit, tongue_len + tongue_end + 0.01, tongue_z1 - tongue_z0 + 2*tongue_fit]);
    // ---- insert holes ----
    for (l = feather_m25) insert_hole(feather_xy(l), s_feather);
    for (l = dac_m25)     insert_hole(dac_xy(l), s_dac);
    for (l = sd_m25)      insert_hole(sd_xy(l), s_sd);
    for (p = strap_posts()) insert_hole(p, s_strap);
    // ---- through-holes in the back ----
    for (h = holes) { p = bp(h[0], h[1]);
      translate([p[0], p[1], 0]) cylinder(d = m3_clear, h = T + 1);
      translate([p[0], p[1], T - 1.8]) cylinder(d = m3_head, h = 2); }
    for (x = boss_x) {
      translate([x, boss_y, 0]) cylinder(d = m2_clear, h = T + 1);
      translate([x, boss_y, T - 1.3]) cylinder(d1 = m2_clear, d2 = m2_head + 0.6, h = 1.31);
    }
    // ---- top edge: Inkplate USB-C, book microSD, power button ----
    let(p = bp(usb_x, 0)) {
      slot_x(p[0], H - wall/2, usb_z, 9.6, 3.8, wall + 2);
      translate([p[0] - 6.5, H - 0.8, usb_z - 3.6]) cube([13, 2, 7.2]);  // room for the plug's overmould
    }
    let(p = bp(sd_x, 0))  slot_x(p[0], H - wall/2, sd_z, 12.6, 2.4, wall + 2);
    let(p = bp(pwr_x, 0)) translate([p[0], H + 1, pwr_z]) rotate([90,0,0]) cylinder(d = 2.2, h = wall + 2);
    // the power button's body overhangs the PCB edge by ~0.9 mm: notch the wall for it
    let(sw = [for (r = inkplate_parts) if (r[5] == "SW1") r][0])
      translate([bx0 + sw[0] - 0.4, H - wall - 0.01, z_pcb_back - 0.3]) cube([sw[1] - sw[0] + 0.8, 1.0, sw[4] + 0.8]);
    // ---- bottom edge: WAKE plunger, audio power switch ----
    let(p = bp(wake_x, board_h)) slot_x(p[0], wall/2, wake_z, 3.5, 3.5, wall + 2);
    translate([slide_x + bx0, wall/2, slide_z]) rotate([90,0,0]) linear_extrude(wall + 2, center=true) square([5.0, 2.6], center=true);
    // ---- side page keys ----
    let(p = bp(0, page_y)) translate([-1, p[1], key_z]) rotate([0,90,0]) cylinder(d = key_hole, h = wall + 2);
    let(p = bp(board_w, page_y)) translate([W - wall - 1, p[1], key_z]) rotate([0,90,0]) cylinder(d = key_hole, h = wall + 2);
    // ---- X=0 wall (right side when held): music microSD, Feather USB, headphone jack ----
    slot_y(wall/2, sd_card[1], sd_card_z, 12.6, 2.6, wall + 2);
    slot_y(wall/2, feather_usb[1], feather_usb_z, 8.6, 4.0, wall + 2);
    translate([-0.01, feather_usb[1] - 5.5, feather_usb_z - 3.5]) cube([0.8, 11, 7]);   // room for the plug's overmould
    translate([-1, dac_jack[1], jack_z]) rotate([0,90,0]) cylinder(d = 5.2, h = wall + 2);
    // WAKE plunger channel through the guide tube
    let(p = bp(wake_x, board_h)) translate([p[0] - 1.75, -1, wake_z - 1.75]) cube([3.5, p[1] + 2, 3.5]);
  }
}

// ---------- tray helpers: standoffs, inserts, pockets, cradle ----------
module standoff(p, h, d) {                 // rises from the inside of the back toward the front
  translate([p[0], p[1], cavity - h]) cylinder(d = d, h = h + 0.01);
}
module insert_hole(p, h) {
  if (h > 0) {
    assert(h + back - (ins25_len + 0.5) >= min_floor - 0.01, "standoff too short for the insert");
    translate([p[0], p[1], cavity - h - 0.01]) cylinder(d = ins25_d, h = ins25_len + 0.5);
    translate([p[0], p[1], cavity - h - 0.01]) cylinder(d1 = ins25_d + 0.8, d2 = ins25_d, h = 0.4);  // lead-in
  }
}
// battery footprint in model coordinates (X0, Y0 = its lower right corner as held)
batt_X0 = bx0 + batt_xoff;
batt_Y0 = by_top - batt_ytop - batt_l;
function strap_posts() = [[batt_X0 - 3.6, by_top - strap_ytop], [batt_X0 + batt_w + 3.6, by_top - strap_ytop]];
module battery_cradle() {
  c = 0.4; t = 1.2; leg = 8; h = 4.0;
  for (i = [0, 1], j = [0, 1]) {
    x = i == 0 ? batt_X0 - c - t : batt_X0 + batt_w + c;
    y = j == 0 ? batt_Y0 - c - t : batt_Y0 + batt_l + c;
    xs = i == 0 ? 0 : -(leg - t);  ys = j == 0 ? 0 : -(leg - t);
    translate([x + xs, y, cavity - h]) cube([leg, t, h + 0.01]);
    translate([x, y + ys, cavity - h]) cube([t, leg, h + 0.01]);
  }
}
// Switch seats. A 6x6 through-hole tact switch has its legs on two opposite faces,
// 6.5 mm apart, reaching ~3.5 mm past the base. Everything below grips the other two
// faces and pushes on the base only BETWEEN the leg rows, so the legs (and the wires
// soldered to them) hang free.
leg_gap = tact_leg_pitch/2 - 0.75;                 // half-width that fits between the leg rows

// Page-key pocket on the X = 0 wall (mirrored for the other wall). The switch lies on
// its side, plunger against the key, legs pointing toward the TOP and BOTTOM of the
// case. It drops in from the front before the Inkplate goes in: the legs slide down
// either side of the back stop, and the stop pushes on the base between them.
module key_pocket() {
  y  = bp(0, page_y)[1];
  px = wall + key_flange_t + key_slack;            // plunger tip
  xb = px + tact_h;                                // switch base
  // The key goes in from inside: lowered in beside the wall, then pushed out through
  // its hole. The pocket starts beyond the key's full length so nothing is in the way.
  x0 = max(px + 0.8, wall + key_flange_t + wall + 1.0 + 0.3);
  g  = tact_body/2 + tact_fit;  t = 1.2;
  // cheeks on the leg faces, stopping before the legs come out near the base
  for (sy = [-1, 1]) translate([x0, sy > 0 ? y + g : y - g - t, key_z - g])
    cube([xb - 1.2 - x0, t, cavity - (key_z - g) + 0.01]);
  // floor against the body's back face (no legs on that face), running on to the stop
  translate([x0, y - g - t, key_z + g]) cube([xb + 2.0 - x0, 2*(g + t), cavity - key_z - g + 0.01]);
  // back stop on the base, between the leg rows, hanging from the floor
  translate([xb, y - leg_gap, key_z - g]) cube([2.0, 2*leg_gap, 2*g + 0.01]);
}
// Chin-key seat: the switch sits plunger-up toward the bezel, legs toward the left
// and right. A pedestal between the leg rows carries the base; two low ribs on the
// leg-free faces keep it centred under its key.
module chin_seat(cx) {
  cy = chin_key_y;
  zb = plate + key_flange_t + key_slack + tact_h;  // switch base
  g  = tact_body/2 + tact_fit;  t = 1.2;
  translate([cx - leg_gap, cy - g - t, zb]) cube([2*leg_gap, 2*(g + t), cavity - zb + 0.01]);
  for (sy = [-1, 1]) translate([cx - leg_gap, sy > 0 ? cy + g : cy - g - t, zb - 1.5]) cube([2*leg_gap, t, 1.51]);
}
// slide switch (SS12D00 style, 8.6 x 3.6 body) pocket behind the bottom-wall slot
module slide_pocket() {
  bw = 8.8; bd = 4.2; t = 1.2; zb = slide_z - 2.0;
  x = slide_x + bx0;
  for (sx = [-1, 1]) translate([x + sx*(bw/2 + t/2) - t/2, wall - 0.01, zb]) cube([t, bd + t, cavity - zb + 0.01]);
  difference() {                                   // back bar with a gap for the pins
    translate([x - bw/2 - t, wall + bd, zb]) cube([bw + 2*t, t, cavity - zb + 0.01]);
    translate([x - 3.5, wall + bd - 1, zb - 1]) cube([7, t + 2, 4.5]);
  }
  translate([x - bw/2 - t, wall - 0.01, slide_z + 1.8]) cube([bw + 2*t, bd + t, cavity - slide_z - 1.8 + 0.01]);
}

// =====================================================================
//  KEYS: 3 chin keys, 2 side page keys, 1 wake plunger (print flat)
// =====================================================================
// Icons, drawn as you see them on the key top (flat face 5.4 mm across)
module icon_skip()  { for (dx = [-2.0, 0.1]) translate([dx, 0]) polygon([[0, -1.4], [0, 1.4], [1.9, 0]]); }   // >> ; turn the key round for <<
module icon_play()  { translate([-2.0, 0]) polygon([[0, -1.4], [0, 1.4], [1.7, 0]]);                          // play / pause
                      for (x = [0.3, 1.4]) translate([x, -1.3]) square([0.6, 2.6]); }
module icon_page()  { polygon([[-1.2, -1.7], [-1.2, 1.7], [1.6, 0]]); }                                       // one arrow
// A key: square flange (print side), round shaft, and the child shape sunk into its top
module key(shaft_len) {
  top = key_flange_t - 0.01 + shaft_len;
  difference() {
    union() {
      translate([-key_flange/2, -key_flange/2, 0]) cube([key_flange, key_flange, key_flange_t]);
      translate([0, 0, key_flange_t - 0.01]) cylinder(d = key_d, h = shaft_len);
    }
    translate([0, 0, top - 0.6]) difference() {            // chamfered rim
      cylinder(d = key_d + 1, h = 1);
      cylinder(d1 = key_d, d2 = key_d - 1.0, h = 0.6);
    }
    if (icon_depth > 0) translate([0, 0, top - icon_depth]) linear_extrude(icon_depth + 0.1) children();
  }
}
module wake_plunger() {
  p = bp(wake_x, board_h);
  len = p[1] + 1.5;                       // pokes 1.5 mm out of the bottom edge
  translate([0, 0, 0]) cube([3.0, len, 3.0]);
  translate([-1, len - 4, 0]) cube([5.0, 1.2, 3.0]);   // stop: can't fall out
}
module battery_strap() {
  span = strap_posts()[1][0] - strap_posts()[0][0];
  L = span + 9;
  difference() {
    translate([0, 0, 0]) hull() for (x = [strap_w/2, L - strap_w/2]) translate([x, strap_w/2, 0]) cylinder(d = strap_w, h = strap_t);
    for (x = [4.5, 4.5 + span]) translate([x, strap_w/2, -1]) {
      cylinder(d = m25_clear, h = strap_t + 2);
      translate([0, 0, 1 + strap_t - m25_head_h]) cylinder(d = m25_head_d, h = m25_head_h + 1);
    }
  }
}
module keys() {
  // chin keys poke out 1 mm: two skip keys (one goes in turned round) and play/pause
  translate([12, 12, 0]) key(plate + 1.0) icon_skip();
  translate([25, 12, 0]) key(plate + 1.0) icon_play();
  translate([38, 12, 0]) key(plate + 1.0) icon_skip();
  for (i = [0:1]) translate([12 + i*13, 28, 0]) key(wall + 1.0) icon_page();   // side page keys
  translate([50, 4, 0]) wake_plunger();
  translate([2, 40, 0]) battery_strap();                            // flat side down; heads recess on top
}

// =====================================================================
//  FIT TEST: the top 14 mm of the tray and of the bezel: ports + top tongues
// =====================================================================
module fit_test() {                         // tray top strip + bezel top strip (tongue check)
  intersection() { tray(); translate([-1, H - 14, -1]) cube([W + 2, 15, T + 2]); }
}
module fit_test_bezel() {
  intersection() { bezel(); translate([-1, H - 14, -1]) cube([W + 2, 15, T + 2]); }
}

// KEY TEST: one page-key pocket, one chin seat and the bezel over it, plus a key of
// each kind. Print it with a couple of switches to hand before the full case.
module key_test() {
  py = bp(0, page_y)[1];
  kx = bx0 + chin_keys_x[0];
  // side wall + page-key pocket, printed back down
  translate([0, 18, T]) rotate([180,0,0]) translate([1, -(py - 9), 0])
    intersection() { tray(); translate([-1, py - 9, -1]) cube([16, 18, T + 2]); }
  // chin seat, printed back down
  translate([22, 20, T]) rotate([180,0,0]) translate([-(kx - 8), 1, 0])
    intersection() { tray(); translate([kx - 8, -1, -1]) cube([17, 20, T + 2]); }
  // the bezel's chin over that seat, printed face down
  translate([46, 0, 0]) translate([-(kx - 8), 1, 0])
    intersection() { bezel(); translate([kx - 8, -1, -1]) cube([17, 20, T + 2]); }
  translate([75, 6, 0]) key(plate + 1.0) icon_play();
  translate([75, 19, 0]) key(wall + 1.0) icon_page();
}

// =====================================================================
//  ASSEMBLY preview: board, panel and suggested audio part positions
// =====================================================================
module inkplate_blocks() {
  for (r = inkplate_parts) translate([bx0 + r[0], by_top - r[3], z_pcb_back]) cube([r[1] - r[0], r[3] - r[2], r[4]]);
}
module board_slab(o, lx, ly, xy, s) {         // PCB + a box for its tallest parts (preview)
  pts = [xy([0,0]), xy([lx,0]), xy([0,ly]), xy([lx,ly])];
  x0 = min([for (p = pts) p[0]]); x1 = max([for (p = pts) p[0]]);
  y0 = min([for (p = pts) p[1]]); y1 = max([for (p = pts) p[1]]);
  translate([x0, y0, zface(s) - pcb_t]) cube([x1 - x0, y1 - y0, pcb_t]);
}
module ghost_parts() {
  color("SteelBlue", 0.35) translate([bx0, by_top - board_h, z_panel]) cube([board_w, board_h, pcb_back]);
  color("DarkSlateGray") inkplate_blocks();
  color("Orange")   translate([batt_X0, batt_Y0, cavity - batt_t]) cube([batt_w, batt_l, batt_t]);
  color("Purple")   board_slab(feather_o, 50.8, 22.86, function(l) feather_xy(l), s_feather);
  color("Teal")     board_slab(dac_o, 38.1, 25.4, function(l) dac_xy(l), s_dac);
  color("Gold")     board_slab(sd_o, 25.4, 22.86, function(l) sd_xy(l), s_sd);
  color("Black")    let(sp = strap_posts()) translate([sp[0][0] - 4.5, sp[0][1] - strap_w/2, cavity - s_strap - strap_t]) cube([sp[1][0] - sp[0][0] + 9, strap_w, strap_t]);
}

if (part == "bezel")    bezel();
if (part == "tray")     translate([0, H, T]) rotate([180,0,0]) tray();   // print back-down
if (part == "keys")     keys();
if (part == "fit_test") {
  translate([0, H, T]) rotate([180,0,0]) fit_test();
  translate([0, -H + 30, 0]) fit_test_bezel();             // prints face down beside it
}
if (part == "key_test") key_test();
if (part == "assembly") {
  color("DimGray") bezel();
  color("Gainsboro", 0.9) tray();
  ghost_parts();
}
