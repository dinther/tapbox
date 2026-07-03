use <boards.scad>

$fn=32;

width = 105;
length = 110;
height = 40;
cr = 10;
er = 1.5;
thickness = 3;


module tube(outer_radius=10, inner_radius=8, height=20){
    difference(){
        cylinder(r=outer_radius, h=height);
        translate([0,0,-1]) cylinder(r=inner_radius, h=height + 2);
    }
}

module donut(radius=10, height=2){
    r = height * 0.5;
    rotate_extrude(angle=360) translate([radius - r,0,0]) circle(d=height);
}

module rounded_disk(radius=10, height=2){
    hull() donut(radius, height);
}

module round_rounded_rect(width, length, height, corner_radius, edge_radius){ 
    w = width * 0.5;
    l = length * 0.5;
    h = height * 0.5;
    hull(){
        translate([-w+corner_radius, -l+corner_radius,0]) rounded_disk(cr, height);
        translate([-w+corner_radius, l-corner_radius,0]) rounded_disk(cr, height);
        translate([w-corner_radius, -l+corner_radius,0]) rounded_disk(cr, height);
        translate([w-corner_radius, l-corner_radius,0]) rounded_disk(cr, height);
    }
}

module round_rounded_cube(width, length, height, corner_radius, edge_radius){
    hull(){
        translate([0,0,er]) round_rounded_rect(width, length, er*2, cr, er);
        translate([0,0,height - er]) round_rounded_rect(width, length, er*2, cr, er);
    }
}

module case_base(){
    t = thickness;
    w = width * 0.5;
    l = length * 0.5;
    difference(){
        union(){
            round_rounded_cube(width, length, 5, cr, er);
            translate([0,0,4.5]) round_rounded_cube(width, length, 31.5, cr, er);
        }
        difference(){
            translate([0,0,t]) round_rounded_cube(width-t-t, length-t-t, 50, cr-t, er);
            // remove parts so it gets left behind
            
            //  corner posts
            translate([-w+t,-l+t,0]) cube([12,12,height]);
            translate([-w+t,l-t-12,0]) cube([12,12,height]);
            translate([w-t-12,-l+t,0]) cube([12,12,height]);
            translate([w-t-12,l-t-12,0]) cube([12,12,height]);
            
            //  extra thickness for LAN port
            translate([w-t-1.3,14,0]) cube([2,25.2, 23]);
            
            //  posts for display board
            translate([21.5,56.5,0]) rotate([30,0,0]) cube([16, 10, 40]);
            translate([21.5,-66,14]) rotate([-45,0,0]) cube([16, 14, 40]);
        }
        
        //  Bolt holes
        translate([-w+cr, -l+cr,t]) cylinder(d=4.6, h=height);
        translate([-w+cr, l-cr,t]) cylinder(d=4.6, h=height);
        translate([w-cr, -l+cr,t]) cylinder(d=4.6, h=height);
        translate([w-cr, l-cr,t]) cylinder(d=4.6, h=height);
        
        //  RJ 45 hole
        translate([-7.2,14,thickness+2]) wt32_eth01(cut=true);
        
        //  USB C hole
        translate([49.3,-39,thickness+4]) rotate([0,0,90]) TP4056(cut=true);
        translate([55.5,-21.7,t+7]) cube([10,13,7.5], center=true);
             
        // Hole for power switch
        translate([46.1,0,thickness + 8]) switch(cut=true);
        
        //  Flat area for display board
        translate([29.0,4.15,41]) cube([17, 83, 20], center=true);
        
        //  Flat area for menu button
        translate([29.0,-40,44.5]) cube([17, 14, 20], center=true);
        
        //  Mounting holes for Display board
        translate([29, 2.15,31]) rotate([0,0,-90]) 8_digit_7_segment_max9219_display_module_holes();
        
        //  Foot pad recesses
        translate([-w+cr, -l+cr,-1]) cylinder(d=8.8, h=2);
        translate([-w+cr, l-cr,-1]) cylinder(d=8.8, h=2);
        translate([w-cr, -l+cr,-1]) cylinder(d=8.8, h=2);
        translate([w-cr, l-cr,-1]) cylinder(d=8.8, h=2);
    }
    
    //  lock bracket mounting posts for WT32 board
    translate([-7.5,10.8,0]) cylinder(d=6, h=7+thickness);
    translate([-7.5,42.5,0]) cylinder(d=6, h=7+thickness);
      
    //  Lock and support for tp4056 board
    translate([30.0,-20.3,0]) tube(3, 1.4, 4+thickness);
    difference(){
        translate([33,-40,thickness]) cube([17.4,28,4]);
        //  Mounting hole for TP4056
        translate([46.3,-14.2,-1]) cylinder(d=2.8, h=6+thickness);
    }
   
    //  Battery holder
    translate([-36,-35,t]) rotate([0,0,90]) 26650_battery_bulkhead();
    translate([-36,35,t]) rotate([0,0,-90]) 26650_battery_bulkhead();
    
    //  Marking on off switch
    translate([52,5,thickness + 11]) rotate([90,0,90]) linear_extrude(1) text("1", size=4);
    translate([52,-8,thickness + 11]) rotate([90,0,90]) linear_extrude(1) text("0", size=4);
}

module lid(){
    t = thickness;
    w = width * 0.5;
    l = length * 0.5;
    difference(){
        round_rounded_cube(width, length, 5, cr, er);
        //  Bolt holes
        translate([-w+cr, -l+cr,-1]) cylinder(d=5.1, h=height+2);
        translate([-w+cr, l-cr,-1]) cylinder(d=5.1, h=height+2);
        translate([w-cr, -l+cr,-1]) cylinder(d=5.1, h=height+2);
        translate([w-cr, l-cr,-1]) cylinder(d=5.1, h=height+2);
        
        //  Bolt recess
        translate([-w+cr, -l+cr,1.61]) cylinder(d=9.5, h=3.4);
        translate([-w+cr, l-cr,1.61]) cylinder(d=9.5, h=3.4);
        translate([w-cr, -l+cr,1.61]) cylinder(d=9.5, h=3.4);
        translate([w-cr, l-cr,1.61]) cylinder(d=9.5, h=3.4);

        //  display slot
        translate([29, 2.15,-2]) rotate([0,0,-90]) 8_digit_7_segment_max9219_display_module(cut=true);
        
        //  tap button slot
        translate([-10,0,-27]) push_button(cut=true);
        
        //  menu button cutout
        translate([29,-38,-0.8]) menu_switch(cut=true);
        
        //  Branding
        translate([-43,0,5.0-0.6]) rotate([0,0,-90]) linear_extrude(2) text("TapBox", size=8, halign="center", valign="center",spacing=1, direction="ltr", language="en", script="latin");
    } 
    //  Lid bracing
    //translate([10,0,-2.5]) cube([4, width-t-t-0.5, 5], center=true);
    //translate([-29,0,-2.5]) cube([4, width-t-t-0.5, 5], center=true);
}

module switch(cut = false){
    t = cut? 6: 0.3;
    d = cut? 3: 2.5;
    cube([6.9, 15.5, 7.3], center=true);
    
    translate([3.45, 0, 0]) difference(){
        translate([-t*0.5, 0,0]) cube([t, 23.3, 7.3], center=true);
        translate([0, 9.5, 0]) rotate([0,90,0]) cylinder(d=d,h=2.3, center=true);
        translate([0, -9.5, 0]) rotate([0,90,0]) cylinder(d=d,h=2.3, center=true);
    }
    // peg
    translate([7.1, 0, 0]) cube([7.3, 5.3, 3.7], center=true);
    if (cut){    
        translate([7.1, 0, 0]) cube([7.3, 10, 4], center=true);
        translate([4.0, 9.5, 0]) rotate([0,90,0]) cylinder(d=d,h=10, center=true);
        translate([4.0, -9.5, 0]) rotate([0,90,0]) cylinder(d=d,h=10, center=true);
    }
}

module push_button(cut=false){
    w = cut? 24.1 : 23.8;
    l = cut? 18.3 : 18;
    d = cut? 16.0 : 15.7;
    cylinder(d=d, h=31);
    translate([-9, -11.9, 30]) cube([l, w, 10.2]);
}

module battery(radius=26.3){
    translate([0,0,13.15]) rotate([90, 0,0]) cylinder(d=radius, h=66, center=true);
}

module menu_switch(cut=false){
    d = cut? 7.3 : 6.7;
    h = cut? 10 : 3.6;
    s = cut? 12.15 : 12;
    g = cut? 0.2 : 0; //  Gap between round and square bit makes pringint easier. Scrificial layer technique
    translate([0,0,3.1*0.5]) cube([s,s,3.1], center=true);
    translate([0,0,3.1+ g]) cylinder(d=d, h=h);
}

module wt32_bracket(){
    difference(){
        hull(){
            translate([-7.5,10.8,0]) cylinder(d=6+2, h=5+thickness);
            translate([-7.5,42.5,0]) cylinder(d=6+2, h=5+thickness);
        }

        translate([-7.5,10.8,0]) cylinder(d=6, h=4+thickness);
        translate([-7.5,42.5,0]) cylinder(d=6, h=4+thickness);
        
        translate([-7.5, 11.5, -2]) cube([10, 31, 5]);
    }
}

module 26650_battery_bulkhead(){
    spring_plate = 7.8;
    nub_plate = 1.5;
    bl = 65.8;
    bw = 26.3;
    l = bl;
    w = bw;
    h = bw;
    translate([0,0,bw*0.5]) rotate([0,-90,0])
    difference(){
        union(){
            translate([-bw*0.5,-bw*0.5,0]) cube([bw*0.5, bw, 5.1]);
            cylinder(d=bw, h=5.1);
            translate([-bw*0.5,-bw*0.5,-4]) cube([bw*0.5, bw, 8]);
        }
        translate([0,0,-4-1]) cylinder(d=bw+0.3, h=4+ 1);
        translate([4.9, -4.9, -1]) cube([bw,9.8,2.4]);
        translate([-4.9, -4.9, 1 + 0.4]) cube([bw,9.8,0.4]);
        translate([-3.9, -4.4, -1]) cube([bw,8.8,2.4]);
    }
}



//translate([46.1,0,thickness + 8]) switch(cut=false);
//battery_bracket();
//translate([-10,0,thickness+ 4.5]) push_button(cut=false);
//  //translate([-23,-48,thickness+4]) CN6009();
//translate([-36,0,thickness]) battery();
//  //translate([20.9,-35,thickness+4]) hw_107();
//translate([49.3,-39,thickness+4]) rotate([0,0,90]) TP4056();
//translate([-7.2,14,thickness+2]) wt32_eth01();
//translate([0,0,5.2]) wt32_bracket();
//translate([29, 2.15,31]) rotate([0,0,-90]) 8_digit_7_segment_max9219_display_module();
//translate([29,-38,34.5]) menu_switch(cut=false);
case_base();
//translate([0,0,height-4.5]) lid();
