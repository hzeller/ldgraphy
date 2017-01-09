//
// build up module for bracket..
// Create stl with:
// make acme_square_nut_mold.stl

// Measurements in mm
$fn=50;
eps = 0.01;
rail_width = 30.0;
extrusion_size = [15,15]; // x,y
profile_notch_depth = 5;
profile_groove = 3;
screw_top_distance=23.85 - 0.5;  // A little wiggle room to the top.

acme_leadscrew = 8;
nut_scale_factor = 1.8;

 // are there rules of thumb for ideal nut OD/ID proportions?
acme_nut_size = acme_leadscrew * nut_scale_factor;
acme_nut_thick = 5;
nut_case_thick=5;
screw_hole=1.5;
nut_case = acme_nut_size + nut_case_thick;

module nut() {
     cube([acme_nut_thick, acme_nut_size, acme_nut_size], center=true);
}

module screw_holder() {
     difference(){
	  cube(size=nut_case, center=true);

	  nut();  // Square stuff, filled with shapelock.

	  // Space for the leadscrew
	  rotate([0, 90, 0]) translate([0, 0, -50]) cylinder(r=acme_leadscrew/2+1, h=100);
	  screw_dist = nut_case/2 - screw_hole/2 - 1.5;
	  translate([screw_dist, screw_dist, -nut_case/2-eps]) cylinder(r=screw_hole/2, h=nut_case + 2*eps);
	  translate([-screw_dist, screw_dist, -nut_case/2-eps]) cylinder(r=screw_hole/2, h=nut_case + 2*eps);
	  translate([-screw_dist, -screw_dist, -nut_case/2-eps]) cylinder(r=screw_hole/2, h=nut_case + 2*eps);
	  translate([screw_dist, -screw_dist, -nut_case/2-eps]) cylinder(r=screw_hole/2, h=nut_case + 2*eps);
     }
}

// Mounting rig, referenced to the top surface. A block that fits under top
// surface and between the profile blocks right and left.
module mounting_rig() {
     // X-axis can be adjusted later, Y axis needs to comfortably fit.
     translate([0, 0, -7.5]) cube([30, 30-0.5, 15], center=true);
}

module x_screw_assembly() {
     screw_holder();
     hull() {
	  translate([0, 0, screw_top_distance]) mounting_rig();
	  // Exactly at the top of the leadscrew hole
	  translate([-nut_case/2, -nut_case/2, acme_leadscrew/2+1]) cube([nut_case, nut_case, 1]);
     }
}

module top_part() {
     translate([0, 0, screw_top_distance]) rotate([180, 0, 0]) intersection() {
	  x_screw_assembly();
	  translate([-50, -50, 0]) cube(size=100);
     }
}

module bottom_part() {
     translate([0, 0, nut_case/2]) intersection() {
	  x_screw_assembly();
	  translate([-50, -50, -100]) cube(size=100);
     }
}

// print
top_part();
translate([30, 0, 0]) bottom_part();
