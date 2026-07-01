use <8_digit_7_segment_max9219_display_module.scad>

$fn=32;

width = 105;
length = 110;
height = 40;
cr = 10;
er = 1;
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
            //translate([0,0,35.5]) round_rounded_cube(width, length, 5, cr, er);
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
        
        //  micro USB hole
        translate([20.5,-35,thickness+4]) hw_107(cut=true);
        
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
    translate([-5.5,10.5,0]) tube(3, 1.4, 5+thickness);
    translate([-5.5,42.8,0]) tube(3, 1.4, 5+thickness);
    
    //  Slider brackets for HW-107 board
    difference(){
        union(){
            translate([24.5,-18.2,0]) cube([25.5, 5, 10]);
            translate([24.5,-39.5,0]) cube([25.5, 5, 10]);
        }
        translate([21,-35,thickness+4]) hw_107(cut=true);
    }
       
    //  Lock for HW-107 board
    translate([20.9,-21.5,0]) tube(3, 1.4, 4.0+thickness);
    
    //  Mounting posts for CN6009 board
    translate([-23, -48,0]) {
        translate([6.4, 18.3, 0]) tube(3, 1.4, 4+thickness);
        translate([36.6, 2, 0]) tube(3, 1.4, 4+thickness);    
    }
    // Mounting block for battery
    difference(){
        union(){
            translate([-21,25,thickness+6]) cube([10, 12, 12], center=true);
            translate([-21,-20,thickness+6]) cube([10, 12, 12], center=true);
        }
        translate([-36,0,thickness]) battery();
        translate([-19,25,thickness]) cylinder(d=2.8, h=13);
        translate([-19,-20,thickness]) cylinder(d=2.8, h=13);
    }
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

module CN6009(){
    //  PCB
    difference(){
        cube([43.6, 20.8, 1.0]);
        translate([6.4, 18.3, -1]) cylinder(d=3.2, h=3);
        translate([36.6, 2, -1]) cylinder(d=3.2, h=3);
    }
    //  components
    translate([4.5,10.4,0]) cylinder(d=8, h=11.4);
    translate([43.6-4.5,10.4,0]) cylinder(d=8, h=11.4);
    translate([10,1,0]) cube([12,12,8.6]);
    translate([11,15.3,0]) cube([9.3,4.2,11.0]);
    translate([23,0.5,0]) cube([9.7,12.3,2.5]);
    translate([23,0.5,0]) cube([9.7,10.7,5.7]);
}

module hw_107(cut=false){
    e = cut? 5 : 0;
    t = cut? 1.6 : 1.4;
    w = cut? 17.6 : 17.3;
    //  PCB
    difference(){
        cube([28.5, 17.3, t]);
        //  PCB cutout
        translate([-1,(17.3-12.7)*0.5,-1]) cube([1.4+1, 12.7,3.4]);
    }
    //  Micro USB
    translate([28.5 - 5.8+ 1.5,(17.3*0.5)-(7.5*0.5),1.15]) cube([5.8+e,7.5, 2.7]);
    if (cut) translate([30,(17.3*0.5)-(12*0.5),-1]) cube([10, 12, 7]);
}

module wt32_eth01(cut=false){
    e = cut? 2 : 0;
    //  pcb
    difference(){
        cube([55.3, 25.3, 1.3]);
        //  mounting holes
        translate([55.3 - 1.5, 1.5, -1]) cylinder(d=2.1, h=3.3);
        translate([55.3 - 1.5, 25.3-1.5, -1]) cylinder(d=2.1, h=3.3);
    }
    //  RJ45 (2mm longer to ensure it pokes through a sidewall
    translate([55.3-16.9,(25.3*0.5)-(16.4*0.5),1.3]) cube([21.2+e,15.9+0.5, 13.4+ 0.5]);
    
}

module menu_switch(cut=false){
    d = cut? 7.3 : 6.7;
    h = cut? 10 : 3.6;
    s = cut? 12.15 : 12;
    g = cut? 0.2 : 0; //  Gap between round and square bit makes pringint easier. Scrificial layer technique
    translate([0,0,3.1*0.5]) cube([s,s,3.1], center=true);
    translate([0,0,3.1+ g]) cylinder(d=d, h=h);
}

module battery_bracket(){
    difference(){
        union(){
            translate([-22,25,thickness+6+13]) cube([12, 12, 9], center=true);
            translate([-22,-20,thickness+6+13]) cube([12, 12, 9], center=true);
        }
        translate([-36,0,thickness]) battery(radius=26);
        translate([-19,25,thickness+14]) cylinder(d=3.2, h=13);
        translate([-19,-20,thickness+14]) cylinder(d=3.2, h=13);
    }
}

module wt32_bracket(){
    //32.3
    difference(){
        hull(){
            translate([-6.5, 10.5, 0]) cylinder(d=8, h=6);
            translate([-6.5, 42.8, 0]) cylinder(d=8, h=6);
        }

        translate([-5.5, 10.5, -1]) cylinder(d=6.5, h=4);
        translate([-5.5, 42.8, -1]) cylinder(d=6.5, h=4);
        
        translate([-6, 11.5, -2]) cube([10, 31, 5]);
        
        // bolt holes
        translate([-5.5,10.5,-1]) cylinder(d=3.2, h=6+2);
        translate([-5.5,42.8,-1]) cylinder(d=3.2, h=6+2);        
    }
}


//translate([46.1,0,thickness + 8]) switch(cut=false);
//battery_bracket();
//translate([-10,0,thickness+ 4.5]) push_button(cut=false);
//translate([-23,-48,thickness+4]) CN6009();
//translate([-36,0,thickness]) battery();
//translate([20.9,-35,thickness+4]) hw_107();
//translate([-7.2,14,thickness+2]) wt32_eth01();
//translate([0,0,5.2]) wt32_bracket();
//translate([29, 2.15,31]) rotate([0,0,-90]) 8_digit_7_segment_max9219_display_module();
//translate([29,-38,34.5]) menu_switch(cut=false);
//case_base();
translate([0,0,height-4.5]) lid();
