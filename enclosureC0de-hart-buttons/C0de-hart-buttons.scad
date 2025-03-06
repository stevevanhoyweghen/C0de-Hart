/*
C0de Hart pushbuttons enclosure
Steve Van Hoyweghen
V1.0
20250303

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <https://unlicense.org/>
*/
use <MCAD/boxes.scad>
// Uses outside dimensions in function calls

/* [Main] */
// Inside width
box_w = 230.0;
// Inside Length
box_l = 120.0;
// Inside Height
box_h = 40.0;
// Corner Radius
box_r = 60.0;
// Height lid
lid_h = 7.0;
// Height lip
lip_h = 1.4;
// Wall Thickness
wall = 3.0;
// Button diameter
button_d = 24;
// Button center x offset
button_x_offset = 56.5;
// Support diameter
support_d = 8.0;
// Lid screw head
screw_head_d = 5.6;
// Lid screw diameter grip
screw_d_grip = 2.7;
// Lid screw diameter free
screw_d_free = 3.4;
// Lid screw head height free
screw_h_head = 2.0;
// Lid screw sink
screw_sink =1.0;
// USB wire diameter
wire_d = 4.0;

pcb_support_screw_d = 1.75;
pcb_support_d = 4;
pcb_support_h = 18;
pcb_support_w = 46.7;
pcb_support_l = 23.3;

pcb_shift = 30;
pcb_w = 51.4;
pcb_l = 28.3;
pcb_show = false;

// Reinforcements
reinforcement_w = 2;
reinforcement_l = 10;
reinforcement_shift = 32;

// Strain relief
strain_relief_l = 10;
strain_relief_hole_d = 3;

/* [Advanced] */
// Only round the side of the box?
sidesonly = 1; // [0:No, 1:Yes]
// How far to seperate the pieces when printing both
separation = 5;
// How snugly should the parts fit?
fit = 0.4; // [0:Super fit, 0.2:Force fit, 0.3:Hold fit, 0.4:Slide fit, 0.5:Free fit]
// Should there be a gap inside so the outside lips fit flush? (Does not change box height)
lip_gap = 0.4; // [0:No gap, 0.1:Small gap, 0.3:Medium gap, 0.5:Large gap], also leave gab between lid and box supports to make sure we have a snug fit

// Number of segments in a circle
$fn = 90;

/* [Hidden] */
// Get outside dimensions
w = box_w + 2 * wall;
l = box_l + 2 * wall;
h = box_h + wall;
r = (box_r > 0 ? box_r + wall : 0);

strain_relief_h = h-lip_h;
strain_relief_w = wire_d + 1;

// Box
translate([0, l + separation, 0])
    union() {
        difference() {
            union() {
                box(w, l, h-lip_gap, wall, r);

                 // Strain relief
                translate([strain_relief_w - strain_relief_w/2, -box_l/2, strain_relief_h - strain_relief_l])
                    rotate([0, -90, 0])
                        rotate_extrude(angle = 90, start = 0, convexity = 2)
                            difference() {
                                    square([strain_relief_l, strain_relief_w], center=false);
                                    translate([strain_relief_l, strain_relief_w/2, 0])
                                        circle(d=wire_d);
                                }
                translate([-strain_relief_w/2, -box_l/2, 0])
                    difference() {
                        cube(size = [strain_relief_w, strain_relief_l, strain_relief_h-strain_relief_l], center = false);
                        translate([0, (strain_relief_l-strain_relief_hole_d/4)/2, strain_relief_h * 0.65])
                            rotate([0, 90, 0])
                                cylinder(h = strain_relief_w, d = strain_relief_hole_d, center = false);
                        translate([0, (strain_relief_l-strain_relief_hole_d/4)/2, strain_relief_h * 0.3])
                            rotate([0, 90, 0])
                                cylinder(h = strain_relief_w, d = strain_relief_hole_d, center = false);
                        translate([strain_relief_w/2, strain_relief_l, 0])
                            rotate([0, 0, 90])
                                cylinder(h = strain_relief_h, d = wire_d, center = false);
                  }
            }

            // Inner lip
            lip(w , l , h , (wall + fit) / 2, r, lip_h-lip_gap);

            // USB wire hole
            usb_wire_hole(wall, h, lip_h, box_l);

            // Push buttons holes
            translate([button_x_offset, 0, 0])
                cylinder(h = wall, d = button_d, center = false);
            translate([-button_x_offset, 0, 0])
                cylinder(h = wall, d = button_d, center = false);
        }

        // Central support
        support(wall, h - lip_gap, support_d, screw_d_grip);

        // PCB supports
        if (pcb_show) { // Make print visible as design help
            color( c = [0.8, 0.2, 0.7], alpha = 0.6 )
                translate([-pcb_w/2, -pcb_l/2 + pcb_shift, pcb_support_h])
                    cube([pcb_w, pcb_l, 1], center = false);
        }
        translate([-pcb_support_w/2, -pcb_support_l/2 + pcb_shift, 0])
            support(wall, pcb_support_h, pcb_support_d, pcb_support_screw_d);
        translate([-pcb_support_w/2,  pcb_support_l/2 + pcb_shift, 0])
            support(wall, pcb_support_h, pcb_support_d, pcb_support_screw_d);
        translate([ pcb_support_w/2, -pcb_support_l/2 + pcb_shift, 0])
            support(wall, pcb_support_h, pcb_support_d, pcb_support_screw_d);
        translate([ pcb_support_w/2,  pcb_support_l/2 + pcb_shift, 0])
            support(wall, pcb_support_h, pcb_support_d, pcb_support_screw_d);

        // Reinforcements
        reinforcement_h = h-lip_h+lip_gap;
        translate([-reinforcement_w/2-reinforcement_shift, box_l / 2, 0])
            reinforcement(reinforcement_h, reinforcement_w, reinforcement_l, 180);
        translate([-reinforcement_w/2+reinforcement_shift, box_l / 2, 0])
            reinforcement(reinforcement_h, reinforcement_w, reinforcement_l, 180);
        translate([reinforcement_w/2-reinforcement_shift, -box_l / 2, 0])
            reinforcement(reinforcement_h, reinforcement_w, reinforcement_l, 0);
        translate([reinforcement_w/2+reinforcement_shift, -box_l / 2, 0])
            reinforcement(reinforcement_h, reinforcement_w, reinforcement_l, 0);
    }

// Lid
difference() {
    union() {
        box(w, l, lid_h-lip_h, wall, r);;
        // Outer lip
        lip(w, l, lid_h, (wall - fit) / 2, r, lip_h);
        // Central support
        //color( c = [0.8, 0.9, 0.7], alpha = 1.0 )
        support(wall, lid_h-lip_h, support_d, screw_d_free);
    }
    union() {
        // USB wire hole
        usb_wire_hole(wall, lid_h, lip_h, box_l);
        // Screw hole
        //cylinder(h = lid_h, d1 = screw_head_d, d2 = screw_d_free, center = false);
        screw_hole(screw_head_d, screw_d_free, screw_h_head, screw_sink, lid_h);
    }
}

module reinforcement(reinforcement_h, reinforcement_w, reinforcement_l, rotation) {
    rotate([0,-90,rotation])
        linear_extrude(height = reinforcement_w, center = false, convexity = 10, twist = 0)
            polygon(points=[[0,0],[reinforcement_h,0],[0,reinforcement_l]], paths=[[0,1,2]]);
}

module screw_hole(screw_head_d, screw_d_free, screw_h_head, screw_sink, height) {
    union() {
        cylinder(h = screw_sink, d = screw_head_d, center = false);
        translate([0,0,screw_sink])
            cylinder(h = screw_h_head, d1 = screw_head_d, d2 = screw_d_free, center = false);
        translate([0,0,screw_h_head+screw_sink])
            cylinder(h = height - screw_h_head, d = screw_d_free, center = false);
    }
}

module box(width, length, height, wall, radius) {
    rotate([0,180,0])
        translate([-width/2, -length/2, -height])
            if (radius > 0) // Rounded?
                difference() {
                    roundedCube([width, length, height], radius, sidesonly, center = false);
                    translate([wall, wall, 0])
                        roundedCube([width - 2*wall, length - 2*wall, height - wall], radius - wall, sidesonly, center = false);
                }
            else
                difference() {
                    cube([width, length, height], center = false);
                    translate([wall, wall, 0])
                        cube([width - 2*wall, length - 2*wall, height - wall], center = false);
                }
}

// Lip connecting lid and box
module lip(width, length, height, wall, radius, thick) {
	intersection() {
		box(width, length, height, wall, radius);
		translate([-width/2, -length/2, height - lip_h])
            cube([width , length , thick], center = false);
	}
}

module support(wall, height, outer_diameter, inner_diameter) {
    translate([0, 0, wall])
        union() {
            difference() {
                cylinder(h = height - wall, d = outer_diameter, center = false);
                cylinder(h = height - wall, d = inner_diameter, center = false);
            }
            cylinder(h = wall, d1 = 1.5*outer_diameter, d2 = outer_diameter, center = false);
        }
}

module usb_wire_hole(wall, height, lip_h, box_l) {
    rotate([90, 0, 0])
        union() {
            translate([0, height - lip_h, box_l/2-4*wall])
                cylinder(wall*5+1, d = wire_d, center = false);
            translate([-wire_d/2, height - lip_h, box_l/2-1])
                cube(size = [wire_d, height-lip_h, wall+1], center = false);
        }
}
