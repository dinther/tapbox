use <boards.scad>
use <pathbuilder.scad>

$fn=32;

width = 105;
length = 110;
height = 40;
cr = 5;
ser = 0.75;
ber = 1.0;
chamfer = 6;
thickness = 3;
bolt_offset = 11;
lid_height = 6;



module tube(outer_radius=10, inner_radius=8, height=20){
    difference(){
        cylinder(r=outer_radius, h=height);
        translate([0,0,-1]) cylinder(r=inner_radius, h=height + 2);
    }
}

module ledge(height=4, width=2, length=10){
    rotate([90,0,90]) linear_extrude(length) M(0,-height)
    v(height)
    h(width);
}

module donut(radius=10, height=2){
    r = height * 0.5;
    rotate_extrude(angle=360) translate([radius - r,0,0]) circle(d=height);
}

module round_rounded_rect(width, length, corner_radius, edge_radius){ 
    w = width * 0.5;
    l = length * 0.5;
    hull(){
        translate([-w+corner_radius, -l+corner_radius,0]) donut(corner_radius, edge_radius*2);
        translate([-w+corner_radius, l-corner_radius,0]) donut(corner_radius, edge_radius*2);
        translate([w-corner_radius, -l+corner_radius,0]) donut(corner_radius, edge_radius*2);
        translate([w-corner_radius, l-corner_radius,0]) donut(corner_radius, edge_radius*2);
    }
}

module round_chamfered_rect(width, length, chamfer, corner_radius, edge_radius){
    w = width * 0.5;
    l = length * 0.5;
    hull(){
        translate([-w+corner_radius, -l+corner_radius + chamfer,0]) donut(corner_radius, edge_radius*2);
        translate([-w+corner_radius + chamfer, -l+corner_radius,0]) donut(corner_radius, edge_radius*2);
        
        translate([-w+corner_radius + chamfer, l-corner_radius,0]) donut(corner_radius, edge_radius*2);
        translate([-w+corner_radius, l-corner_radius - chamfer,0]) donut(corner_radius, edge_radius*2);
        
        translate([w-corner_radius - chamfer, -l+corner_radius,0]) donut(corner_radius, edge_radius*2);
        translate([w-corner_radius, -l+corner_radius + chamfer,0]) donut(corner_radius, edge_radius*2);
        
        translate([w-corner_radius - chamfer, l-corner_radius,0]) donut(corner_radius, edge_radius*2);
        translate([w-corner_radius, l-corner_radius - chamfer,0]) donut(corner_radius, edge_radius*2); 
    }
}

module round_rounded_cube(width, length, height, corner_radius, bottom_edge_radius, top_edge_radius){
    hull(){
        translate([0,0,bottom_edge_radius]) round_rounded_rect(width, length, corner_radius, bottom_edge_radius);
        translate([0,0,height - top_edge_radius]) round_rounded_rect(width, length, corner_radius, top_edge_radius);
    }
}

module round_chamfered_cube(width, length, height, chamfer, corner_radius, bottom_edge_radius, top_edge_radius){
    hull(){
        translate([0,0,bottom_edge_radius]) round_chamfered_rect(width, length, chamfer, corner_radius, bottom_edge_radius);
        translate([0,0,height - top_edge_radius]) round_chamfered_rect(width, length, chamfer, corner_radius, top_edge_radius);
    }    
}

module case_base(){
    t = thickness;
    w = width * 0.5;
    l = length * 0.5;
    difference(){
        union(){
            round_chamfered_cube(width, length, 4, chamfer, cr, ber, ser);
            translate([0,0,4]) round_chamfered_cube(width, length, height-4-lid_height, chamfer, cr, ser, ser);
            translate([-51.4,0,20]) rotate([90,0,-90]) hull(){
                translate([0,-1,1]) round_rounded_rect(width=52, length=25, corner_radius=2.5, edge_radius=0.5);
                translate([0,-1,1.5]) round_rounded_rect(width=48, length=24, corner_radius=2.5, edge_radius=0.5);
            }
        }
        difference(){
            translate([0,0,t]) round_chamfered_cube(width-t-t, length-t-t, 50, chamfer, cr-t, ser, ser);
            // remove parts so it gets left behind
            
            //  corner posts
            translate([-w+t,-l+t,0]) cube([12,12,height]);
            translate([-w+t,l-t-12,0]) cube([12,12,height]);
            translate([w-t-12,-l+t,0]) cube([12,12,height]);
            translate([w-t-12,l-t-12,0]) cube([12,12,height]);
            
            //   Branding slot rail
            translate([-49.5,-40, thickness]) cube([1.2, 80, height-thickness-lid_height]);
            //translate([-49.3,18.5, thickness]) cube([1.2, 9, height-thickness-lid_height-4]);
            
            //  extra thickness for LAN port
            translate([w-t-1.3,14,0]) cube([2,25.2, 23]);
            
            //  posts for display board
            translate([21.5,56.5,0]) rotate([33,0,0]) cube([16, 10, 40]);
            translate([21.5,-66,14]) rotate([-47,0,0]) cube([16, 14, 40]);
        }
        
        //  Bolt holes
        translate([-w+bolt_offset, -l+bolt_offset,t]) cylinder(d=4.6, h=height);
        translate([-w+bolt_offset, l-bolt_offset,t]) cylinder(d=4.6, h=height);
        translate([w-bolt_offset, -l+bolt_offset,t]) cylinder(d=4.6, h=height);
        translate([w-bolt_offset, l-bolt_offset,t]) cylinder(d=4.6, h=height);
        
        //  RJ 45 hole
        translate([-7.2,14,thickness+2]) wt32_eth01(cut=true);
        
        //  USB C hole
        translate([49.3,-39,thickness+4]) rotate([0,0,90]) TP4056(cut=true);
        translate([55.5,-21.7,t+7]) cube([10,13,7.5], center=true);
             
        // Hole for power switch
        translate([46.1,0,thickness + 8]) switch(cut=true);
        
        //  Flat area for display board
        translate([29.0,4.15,38.8]) cube([17.1, 83, 20], center=true);
        
        //  Flat area for menu button
        translate([29.0,-40,44.0-0.7]) cube([17, 14, 20], center=true);
        
        //  Mounting holes for Display board
        translate([29, 2.15,31]) rotate([0,0,-90]) 8_digit_7_segment_max9219_display_module_holes();
        
        //  Foot pad recesses
        translate([-w+bolt_offset, -l+bolt_offset,-1]) cylinder(d=8.8, h=2);
        translate([-w+bolt_offset, l-bolt_offset,-1]) cylinder(d=8.8, h=2);
        translate([w-bolt_offset, -l+bolt_offset,-1]) cylinder(d=8.8, h=2);
        translate([w-bolt_offset, l-bolt_offset,-1]) cylinder(d=8.8, h=2);
        
        //  Microphone hole and slot
        translate([0, 50.4,thickness+10]) rotate([0,0,90]) inmu441_microphone_holder(cut=true);
        
        //  Audio jack hole
        translate([47.5,-21.7,21]) rotate([0,90,0]) cylinder(d=8.2, h=3);
        translate([50.0,-21.7,21]) rotate([0,90,0]) cylinder(d=5.85, h=10);
        
        //  Branding cutout
        translate([-51.4-1,0,19]) rotate([90,0,-90]) hull(){
            translate([0,0,-1.7]) round_rounded_rect(width=40, length=20, corner_radius=2.5, edge_radius=0.5);
            translate([0,0,2]) round_rounded_rect(width=42, length=21, corner_radius=2.5, edge_radius=0.5);
        }
        //  Branding card slot
        translate([-49.9,0,21+thickness-1]) rotate([0,0,-90]) cube([44, 1.1, 42], center=true);
    }
    
    //  lock bracket mounting posts for WT32 board
    translate([-7.5,10.8,0]) cylinder(d=6, h=7+thickness);
    translate([-7.5,42.5,0]) cylinder(d=6, h=7+thickness);
      
    //  Lock and support for tp4056 board
    translate([30.0,-20.3,0]) tube(3, 1.4, 4+thickness);
    difference(){
        translate([32,-40,thickness]) cube([18.4,28,4]);
        //  Mounting hole for TP4056
        translate([46.3,-14.2,-1]) cylinder(d=2.8, h=6+thickness);
    }
    
    //  Mounting points for PCM1808 board
    translate([0,-37,thickness + 4]) PCM1808(studs=true, height=4);
   
    //  Battery holder 72.2
    translate([-34.9,-36.1,t]) rotate([0,0,-90]) 26650_battery_bulkhead("+");
    translate([-34.9,36.1,t]) rotate([0,0,90]) 26650_battery_bulkhead("-");
    
    // microphone holder
    translate([0, 50.4,thickness+10]) rotate([0,0,90]) inmu441_microphone_holder();
    translate([1, 50.4+2.0,thickness+10]) rotate([0,0,180]) ledge(3, 3, 18);
    
    //  Marking on off switch
    translate([52,5,thickness + 11]) rotate([90,0,90]) linear_extrude(1) text("1", size=4);
    translate([52,-8,thickness + 11]) rotate([90,0,90]) linear_extrude(1) text("0", size=4);
    
     //  Branding
     //translate([-50,0,20]) rotate([90,0,-90]) linear_extrude(0.7) text("tapbox", size=8, halign="center", valign="center",spacing=1, direction="ltr", language="en", script="latin");
}

module lid(){
    t = thickness;
    w = width * 0.5;
    l = length * 0.5;
    difference(){
        union(){
            hull(){
                round_chamfered_cube(width, length, lid_height*0.5, chamfer, cr, ser, ber);
                translate([0,0,lid_height*0.5]) round_chamfered_cube(width-4, length-4, lid_height*0.5, chamfer, cr, ber, ber);
                
            }
        }
        //  Battery cutouts
        translate([-36,0,-height+thickness+lid_height]) battery();
        
        //  Bolt holes
        translate([-w+bolt_offset, -l+bolt_offset,-1]) cylinder(d=5.1, h=height+2);
        translate([-w+bolt_offset, l-bolt_offset,-1]) cylinder(d=5.1, h=height+2);
        translate([w-bolt_offset, -l+bolt_offset,-1]) cylinder(d=5.1, h=height+2);
        translate([w-bolt_offset, l-bolt_offset,-1]) cylinder(d=5.1, h=height+2);
        
        //  Bolt recess
        translate([-w+bolt_offset, -l+bolt_offset,2.61]) cylinder(d=9.0, h=3.4);
        translate([-w+bolt_offset, l-bolt_offset,2.61]) cylinder(d=9.0, h=3.4);
        translate([w-bolt_offset, -l+bolt_offset,2.61]) cylinder(d=9.0, h=3.4);
        translate([w-bolt_offset, l-bolt_offset,2.61]) cylinder(d=9.0, h=3.4);

        //  display slot
        translate([29, 2.15,-2]) rotate([0,0,-90]) 8_digit_7_segment_max9219_display_module(cut=true);
        
        //  tap button slot
        //translate([-10,0,-27]) push_button(cut=true);
        
        // sanwa button OBSF 30 black
        translate([-10,0,2]) cylinder(d=30, h=lid_height + 2);
        translate([-10,0,-1]) cylinder(d=36, h=2 - 0.3 + 1);
        
        //  menu button cutout
        translate([29,-38, 0]) menu_switch(cut=true); 
     
        //  Display cover cutout
        translate([29,2.2,6-2]) rotate([0,0,90]) round_rounded_cube(width=67, length=21, height=5, corner_radius=2.5, bottom_edge_radius=0.1, top_edge_radius=0.1);  
    } 
}

module switch(cut = false){
    t = cut? 6: 0.3;
    d = cut? 2.8 : 2.5;
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

module inmu441_microphone_holder(cut = false){
    if (cut){
        //  microphone hole
        translate([1.6-0.1,8,8]) rotate([0,90,0]) cylinder(d=4, h=10);
        //  Groove for components to clear
        translate([1.1-0.1,6,4]) cube([2,4,16]);
    } else {
        difference(){
            translate([-1,-1,0]) cube([3,18,12]);
            translate([0.6,1,1]) cube([1,14,16]);
            translate([-3+1,1.5,1]) cube([3,13,16]);
            //  Groove for components to clear
            translate([1.6 - 0.1,6,4]) cube([3,4,16]);
        }
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
    h1 = cut? 3.7 : 3.5;
    h = cut? 10 : 3.6;
    s = cut? 12.10 : 12;
    g = cut? 0.2 : 0; //  Gap between round and square bit makes pringint easier. Scrificial layer technique
    translate([-s*0.5,-s*0.5,0]) cube([s,s,h1]);
    if (cut) translate([0,0,0.5]) cube([s+7,s,1], center=true);
    translate([0,0,h1+ g]) cylinder(d=d, h=h);
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

module 18650_battery_bulkhead_old(){
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
            translate([-bw*0.5,-bw*0.5,0]) cube([bw*0.5, bw, 5.6]);
            cylinder(d=bw, h=5.6);
            translate([-bw*0.5,-bw*0.5,-4]) cube([bw*0.5, bw, 8]);
        }
        translate([0,0,-4-1]) cylinder(d=bw+0.3, h=4+ 1);
        translate([4.9, -5, -1]) cube([bw,10,2.4]);
        translate([4.9, -1.5, -1]) cube([bw,3,8.4]);
        translate([-4.9, -5, 1 + 0.4]) cube([bw,10,0.4]);
        translate([-3.9, -4.4, -1]) cube([bw,8.8,2.4]);
    }
}

module 26650_battery_bulkhead(marker="", slot = true){
    bw = 5;
    fw = 2.6;
    sw = 1.4;
    ss = 6;
    nw = 0.6;
    
    spring_plate = 7.8;
    nub_plate = 1.5;
    bl = 65.8;
    bd = 28.3+2;
    l = bl;
    w = bw;
    h = bw;
    translate([0,0,bd*0.5-2])
    rotate([0,-90,0])
    difference(){
        translate([0,0,0]) union(){
            translate([-bd*0.5,-bd*0.5,-bw]) cube([bd*0.5, bd, bw+fw+ss]);
            translate([0,0,-bw]) cylinder(d=bd, h=bw+fw+ss);
        }
        translate([0,0,fw]) cylinder(d=26.3+0.3, h=ss+ fw+1);
        translate([0,0,-10]) cube([25,25,ss+ fw+1], center=true);
        translate([0,-12,fw]) cube([50,24,20+sw]);  //reduce second value to make tighter
        
        //  slot
        translate([-5, -7, 0]) cube([50,14,nw]);
        translate([5, -7, 0]) cube([50,14,sw]);
        translate([-5, -5, 0]) cube([50,10,fw+10]);
        translate([-3, -1.5, -bw-1]) cube([50,3,bw+2]);
        if (slot){
            translate([5,0,fw+0.25]) cube([20,50,0.5], center=true);
        }
    }
    translate([2,8,23]) rotate([-42,0,0]) linear_extrude(2.5) text(marker, size=7, valign="center", halign="center");
    translate([2,-8,23]) rotate([42,0,0]) linear_extrude(2.5) text(marker, size=7, valign="center", halign="center");
}

//translate([46.1,0,thickness + 8]) switch(cut=false);
//translate([-10,0,thickness+ 4.5]) push_button(cut=false);
//translate([-36,0,thickness]) battery();
//translate([49.3,-39,thickness+4]) rotate([0,0,90]) TP4056();
//translate([0,-37,thickness + 4]) PCM1808();
//translate([-7.2,14,thickness+2]) wt32_eth01();
//translate([0,0,5.2]) wt32_bracket();
//translate([29, 2.15,28.8]) rotate([0,0,-90]) 8_digit_7_segment_max9219_display_module();
//translate([29,-38,height - lid_height]) menu_switch(cut=false);
case_base();
//translate([0,0,height-lid_height]) lid();

