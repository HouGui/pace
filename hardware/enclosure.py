#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""PACE LCD-7 enclosure, revision A. Dimensions in mm; CadQuery 2.8.0.

Frame: origin at front-glass center, x right/y up when viewing the rear,
z positive toward wall. Manufacturer STEP transformation is below.
Generate repository STLs: python hardware/enclosure.py
Validation results are printed to stdout; STEP export is optional.
Optional collision review: --reference-step /path/to/Waveshare.stp
"""
import argparse
import hashlib
import json
from pathlib import Path

import cadquery as cq
import trimesh

REVISION = 'A'
WIDTH, HEIGHT = 188.0, 108.0
FRONT, BACK, WALL = 2.6, 32.0, 2.4
WALL_PLANE = 37.5
MOUNT_X = (-61.61, 64.59)
MOUNT_Y = (-33.8, 31.85)
MOUNT_FACE = 8.5
SLIDE = 12.0
HANGERS = ((-76.0, 8.0), (76.0, 8.0))
WALL_FIXINGS = ((-40.0, 0.0), (40.0, 0.0))


def box(x, y, z, sx, sy, sz):
    return cq.Workplane('XY').box(sx, sy, sz).translate((x, y, z)).val()


def rounded_box(w, h, z0, z1, r):
    return (cq.Workplane('XY').box(w, h, z1-z0)
            .edges('|Z').fillet(r).translate((0, 0, (z0+z1)/2)).val())


def cylinder(x, y, z0, z1, r):
    return cq.Solid.makeCylinder(r, z1-z0, cq.Vector(x,y,z0))


def y_hole(x, y0, y1, z, r):
    return cq.Solid.makeCylinder(r, y1-y0, cq.Vector(x,y0,z), cq.Vector(0,1,0))


def rear_cover():
    shell = rounded_box(WIDTH, HEIGHT, FRONT, BACK, 4)
    shell = shell.cut(rounded_box(WIDTH-2*WALL, HEIGHT-2*WALL,
                                  FRONT-1, BACK-WALL, 2))
    # Service opening: wall plate shields the PCB once installed; lower gap vents.
    shell = shell.cut(box(0,-9,31,122,76,6))
    # Clearance for the touch-panel flex shown in the manufacturer STEP.
    shell = shell.cut(box(4.06,52,5.5,22,5,8))
    for x,y in HANGERS:
        entry = cylinder(x,y-SLIDE,28,33,6.4)
        slot = cylinder(x,y,28,33,3.3).fuse(
            box(x,y-SLIDE/2,30.5,6.6,SLIDE,5))
        shell = shell.cut(entry.fuse(slot))
    # Floor clearance for the anti-lift lug throughout the 12 mm slide.
    shell = shell.cut(box(0,-37,32,15,27,8))
    # Underside locking screw: same M3x25 size as the display screws.
    tab = box(0,-52.4,28.5,18,3.2,7)
    shell = shell.fuse(tab).cut(y_hole(0,-56,-49,29.3,1.7))
    # Cable exits downward at left, with integral tie holes for strain relief.
    shell = shell.cut(box(-84,-53,20,14,7,28))
    shell = shell.cut(box(65,-53,28,8,7,10))  # QT lead, open at the rear edge
    for x in (-90,-76):
        shell = shell.cut(box(x,-46,31,3.2,2,5))
    # Short bridges, open-bottom ventilation slots; rear face is the print bed.
    for x in range(-75,76,10):
        shell = shell.cut(box(x,53,17,3,6,12))
    for x in (-45,-35,-25,25,35,45,55,65,75):
        shell = shell.cut(box(x,-53,17,3,6,12))
    # Posts end on the four outer M3 threaded bosses, not on glass or PCB.
    for x in MOUNT_X:
        for y in MOUNT_Y:
            post = cylinder(x,y,MOUNT_FACE,BACK,4)
            shell = shell.fuse(post)
            shell = shell.cut(cylinder(x,y,7.5,33,1.7))
    # SHT40 cradle below the display: 15 mm open air gap, two thin supports.
    pod = rounded_box(31.4,24,20,32,2).translate((65,-81,0))
    pod = pod.cut(box(65,-81,24.3,27.4,20,10.6))
    pod = pod.cut(box(65,-81,31,16,9,5))
    for x in (49.3,80.7):
        pod = pod.cut(box(x,-81,25,5,8,8))
    for dx in (-10.16,10.16):
        for dy in (-6.35,6.35):
            x,y=65+dx,-81+dy
            pod=pod.fuse(cylinder(x,y,26,32,3.4))
            pod=pod.cut(cylinder(x,y,25,33,1.35))
            if dx*dy<0:  # Two diagonal screws; the other posts support the PCB.
                nut=(cq.Workplane('XY').polygon(6,6.004).extrude(2.3)
                     .translate((x,y,29.8)).val())
                pod=pod.cut(nut)
    for x in (54,76):
        shell=shell.fuse(box(x,-61.5,30.5,3,17,3))
    shell=shell.fuse(pod)
    # A cable tie retains the QT lead on one support, without touching the sensor.
    shell=shell.cut(box(76,-61,30.5,1.2,3.2,5))
    return shell.clean()


def wall_plate():
    plate = rounded_box(168,76,33.5,WALL_PLANE,4)
    for x,y in HANGERS:
        neck = cylinder(x,y,29.3,33.6,3)
        cone = cq.Solid.makeCone(6,3,3,cq.Vector(x,y,26.3))
        cap = cylinder(x,y,25.1,26.3,6)
        plate = plate.fuse(neck).fuse(cone).fuse(cap)
    # Front support lands limit touch-induced movement to 0.2 mm nominal.
    for x in (-82,82):
        for y in (-26,26):
            plate = plate.fuse(box(x,y,32.85,4,8,1.3))
    # Recesses keep the LCD screw heads clear of the plate during insertion.
    for x in MOUNT_X:
        for y in MOUNT_Y:
            plate = plate.cut(cylinder(x,y,32,39,3.5).fuse(
                cylinder(x,y+SLIDE,32,39,3.5)).fuse(
                box(x,y+SLIDE/2,35.5,7,SLIDE,7)))
    for x,y in WALL_FIXINGS:
        plate = plate.cut(cylinder(x,y,32,39,2.5))
        # 90-degree countersink for a 4.5 mm wood screw, head <=9.5 mm.
        plate = plate.cut(cq.Solid.makeCone(4.9,2.5,2.4,cq.Vector(x,y,33.5)))
    lug = box(0,-31,31.5,14,14,12)
    plate = plate.fuse(lug)
    # Top-entry hex well: gravity seats the nut; flats locate it on the screw axis.
    nut_well=(cq.Workplane('XZ').polygon(6,6.697).extrude(9.6)
              .translate((0,-23.9,29.3)).val())
    plate = plate.cut(nut_well)
    plate = plate.cut(y_hole(0,-40,-22,29.3,1.7))
    return plate.clean()


def print_orientation(shape):
    shape=shape.rotate((0,0,0),(1,0,0),180)
    b=shape.BoundingBox()
    return shape.translate((-(b.xmin+b.xmax)/2,-(b.ymin+b.ymax)/2,-b.zmin))


def load_reference(path):
    return (cq.importers.importStep(str(path)).val()
            .rotate((0,0,0),(1,0,0),-90).translate((0,-1.02,.8)))


def overlap_volume(shape, reference):
    """Skip disjoint bounding boxes before exact solid intersections."""
    a = shape.BoundingBox()
    volume = 0.0
    for solid in reference.Solids():
        b = solid.BoundingBox()
        if (a.xmax <= b.xmin or b.xmax <= a.xmin or
                a.ymax <= b.ymin or b.ymax <= a.ymin or
                a.zmax <= b.zmin or b.zmax <= a.zmin):
            continue
        volume += shape.intersect(solid).Volume()
    return volume


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=Path('hardware/stl'),
                        help='Directory for the public STL files')
    parser.add_argument('--step-output',type=Path,
                        help='Optional directory for STEP exports (not needed for printing)')
    parser.add_argument('--reference-step',type=Path)
    parser.add_argument('--sensor-step',type=Path)
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    if args.step_output:
        args.step_output.mkdir(parents=True,exist_ok=True)
    cover,plate=rear_cover(),wall_plate()
    report={'revision':REVISION,'cadquery':cq.__version__,
            'wall_to_glass_mm':WALL_PLANE,'parts':{},
            'physical_fit_load_and_temperature':'NOT TESTED'}
    for name,shape in [('rear-cover',cover),('wall-plate',plate)]:
        assert shape.isValid(),f'{name}: invalid CAD solid'
        printable=print_orientation(shape)
        b=printable.BoundingBox()
        assert max(b.xlen,b.ylen)+6<=200, f'{name}: does not fit 200 mm bed with 3 mm brim'
        cq.exporters.export(printable,str(args.output/f'{name}.stl'),tolerance=.05,angularTolerance=.1)
        if args.step_output:
            cq.exporters.export(printable,str(args.step_output/f'{name}.step'))
        mesh=trimesh.load(args.output/f'{name}.stl',force='mesh')
        assert mesh.is_watertight and mesh.is_winding_consistent and mesh.volume>0
        assert len(mesh.split())==1, f'{name}: disconnected print'
        report['parts'][name]={'size_mm':[round(b.xlen,3),round(b.ylen,3),round(b.zlen,3)],
           'solid_count':len(shape.Solids()),'volume_cm3':round(shape.Volume()/1000,3),
           'stl_watertight':True,'connected_meshes':1,
           'stl_sha256':hashlib.sha256((args.output/f'{name}.stl').read_bytes()).hexdigest()}
    report['mount_interference_mm3']={}
    heads=cq.Compound.makeCompound([cylinder(x,y,32,35,2.75)
                                    for x in MOUNT_X for y in MOUNT_Y])
    for dy in range(13):
        volume=cover.translate((0,dy,0)).intersect(plate).Volume()
        report['mount_interference_mm3'][str(dy)]=round(volume,6)
        assert volume<.01, f'mount collides during slide at {dy}: {volume}'
        assert overlap_volume(plate,heads.translate((0,dy,0)))<.01, 'LCD screw-head collision'
    report['lcd_screw_head_slide_clearance']='PASS at 1 mm intervals'
    lock_nut=(cq.Workplane('XZ').polygon(6,6.351).extrude(2.4)
              .translate((0,-31.1,29.3)).val())
    assert overlap_volume(lock_nut,plate)<.01, 'locking nut does not fit well'
    report['m3_nut_fit']='PASS; top-entry well constrains depth and rotation'
    # Allow a straight plug and a 90-degree cable turn at either USB socket.
    # These are design envelopes, NOT measured CU0174 overmould dimensions.
    # Start the overmould 0.5 mm outside the socket face; the metal tip is inserted.
    plug_envelopes=[box(-69.06,y,11.27,27,12,8) for y in (3.55,-11.55)]
    cable_corridor=box(-85.56,-24.2,11.27,10,59.6,8)
    for envelope in [*plug_envelopes,cable_corridor]:
        assert overlap_volume(envelope,cover)<.01, 'USB envelope overlaps cover'
    report['usb_envelope_mm']={'plug':[27,12,8],'cable_turn_radius':6,
                               'overmould_offset_from_socket':.5,
                               'actual_cable_fit':'NOT MEASURED'}
    if args.reference_step:
        ref=load_reference(args.reference_step)
        report['reference_sha256']=hashlib.sha256(args.reference_step.read_bytes()).hexdigest()
        for name,shape in [('cover',cover),('plate',plate)]:
            volume=overlap_volume(shape,ref)
            report[f'{name}_reference_overlap_mm3']=round(volume,6)
            assert volume<.01, f'{name} overlaps manufacturer model: {volume}'
        for envelope in [*plug_envelopes,cable_corridor]:
            volume=overlap_volume(envelope,ref)
            assert volume<.01, f'USB envelope overlaps manufacturer model: {volume}'
        # Sensor harness and USB plugs are not represented by the supplier model.
        report['reference_scope']='Supplier display model and hypothetical USB envelopes; actual cables need fit check.'
    if args.sensor_step:
        sensor=(cq.importers.importStep(str(args.sensor_step)).val()
                .rotate((0,0,0),(1,0,0),180).translate((52.3,-72.11,26)))
        overlap=overlap_volume(cover,sensor)
        report['sensor_reference_overlap_mm3']=round(overlap,6)
        report['sensor_reference_sha256']=hashlib.sha256(args.sensor_step.read_bytes()).hexdigest()
        assert overlap<.01, f'sensor overlaps cradle: {overlap}'
        sensor_heads=cq.Compound.makeCompound([
            cylinder(65+dx,-81+dy,21.93,24.43,2.25)
            for dx,dy in [(-10.16,6.35),(10.16,-6.35)]])
        assert overlap_volume(sensor_heads,sensor)<.01, 'sensor screw-head collision'
        assert overlap_volume(sensor_heads,cover)<.01, 'sensor head/cradle collision'
        report['sensor_screw_heads']='PASS'
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
