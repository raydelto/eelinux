"""Build the production PS2 DDX and shared Mesa GS components, without test clients."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys
import mesa,userland

ROOT=Path(__file__).resolve().parents[1]
GRAPHICS='ps2_gs_client ps2_gs_surface ps2_gs_render ps2_gs_texture ps2_gs_bitmap ps2_gs_logic ps2_gs_transfer ps2_gs_lut'.split()
MESA='ps2_resource ps2_render_target ps2_owned_draw ps2_sampler ps2_textured_draw ps2_pipe ps2_logic ps2_solid_batch ps2_copy_batch ps2_rop ps2_filter ps2_multiply ps2_divide ps2_wide ps2_wide_filter ps2_gradient ps2_packed ps2_coverage ps2_render_raster ps2_format ps2_plane ps2_blit ps2_composite ps2_tile ps2_cursor ps2_image ps2_planar ps2_polygon ps2_polygon_stipple ps2_polygon_tile ps2_transform'.split()
XORG='ps2_surface_exa ps2_render_bridge ps2_texture_exa ps2_context_bridge ps2_glyph ps2_gc ps2_solid ps2_copy ps2_copy_exa ps2_blit_exa ps2_alpha_map_screen ps2_render_geometry ps2_copy_window ps2_paint_window ps2_fill ps2_cursor_screen ps2_image_gc ps2_polygon_gc ps2_geometry ps2_readback ps2_driver ps2_logic_exa'.split()

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def build(work,prefix,sysroot,host,jobs,staging,diagnostics=False):
    userland.use_native_linux_path();work=work.resolve();work.mkdir(parents=True,exist_ok=True)
    mesa.fetch(work/'mesa')
    lock=json.loads((ROOT/'userland/mesa-sources.json').read_text())
    upstream=work/'mesa'/('mesa-'+lock['version'])/('mesa-'+lock['version'])
    generated=work/'generated';generated.mkdir(exist_ok=True)
    with (work/'build.log').open('w',buffering=1) as log:
        python=host/'bin/python3'
        if subprocess.run([str(python),'-c','import mako'],stdout=log,stderr=log).returncode:
            subprocess.run(['make','-C',str(host.parent),f'BR2_JLEVEL={jobs}',f'-j{jobs}','host-python-mako'],stdout=log,stderr=log,check=True)
        subprocess.run([str(python),str(upstream/'src/compiler/builtin_types_h.py'),str(generated/'builtin_types.h')],stdout=log,stderr=log,check=True)
        with (generated/'nir_opcodes.h').open('w') as output:
            subprocess.run([str(python),str(upstream/'src/compiler/nir/nir_opcodes_h.py')],stdout=output,stderr=log,check=True)
        for name in ('nir_intrinsics_h.py','nir_intrinsics_indices_h.py'):
            subprocess.run([str(python),str(upstream/'src/compiler/nir'/name),'--outdir',str(generated)],stdout=log,stderr=log,check=True)
        flags=[*userland.CPU_FLAGS,'--sysroot='+str(sysroot),'-Os','-std=gnu17','-fPIC','-Wall','-Wextra','-Werror',
            '-DNDEBUG','-DUSE_GCC_ATOMIC_BUILTINS','-DHAVE_ENDIAN_H','-DHAVE_STRUCT_TIMESPEC','-DHAVE_PTHREAD',
            '-isystem'+str(sysroot/'usr/include/xorg'),'-I'+str(sysroot/'usr/include/pixman-1'),
            *['-isystem'+str(upstream/path) for path in ('src','include','src/gallium/include','src/gallium/auxiliary','src/compiler/nir','src/compiler')],
            '-isystem'+str(generated),*['-I'+str(ROOT/path) for path in ('graphics','kernel/new-drivers','desktop/xorg','mesa/ps2')],
            '-include','xorg-server.h']
        if diagnostics:flags.append('-DPS2_DDX_DIAGNOSTICS')
        # Struct layouts must follow the server configuration, particularly
        # PixmapRec's conditional screen_x/screen_y fields.
        macros=subprocess.check_output([prefix+'gcc',*flags,'-dM','-E','-'],input=b'')
        server_composite=b'#define COMPOSITE ' in macros
        server_screensaver=b'#define SCREENSAVER ' in macros
        objects=[];inputs={}
        for folder,names in (('graphics',GRAPHICS),('mesa/ps2',MESA),('desktop/xorg',XORG)):
            for name in names:
                source=ROOT/folder/(name+'.c');obj=work/(name+'.o')
                subprocess.run([prefix+'gcc',*flags,'-c',str(source),'-o',str(obj)],stdout=log,stderr=log,check=True)
                objects.append(str(obj));inputs[source.relative_to(ROOT).as_posix()]=sha(source)
        module=work/'ps2_drv.so'
        subprocess.run([prefix+'gcc',*userland.CPU_FLAGS,'--sysroot='+str(sysroot),'-shared','-Wl,-soname,ps2_drv.so',
            '-o',str(module),*objects,'-latomic','-lpixman-1','-lm'],stdout=log,stderr=log,check=True)
        userland.validate_elf(module)
        destination=staging/'usr/lib/xorg/modules/drivers/ps2_drv.so';destination.parent.mkdir(parents=True,exist_ok=True)
        import shutil
        shutil.copy2(module,destination)
        userland.compile_desktop_helpers(staging,{'compiler':prefix+'gcc'},sysroot)
    for folder in ('graphics','mesa/ps2','desktop/xorg','kernel/new-drivers'):
        for header in (ROOT/folder).glob('*.h'):
            inputs[header.relative_to(ROOT).as_posix()]=sha(header)
    report={'result':'built','module_sha256':sha(module),'inputs':inputs,'flags':flags,
        'mesa_source':lock,'staging':str(staging),'diagnostic_clients_installed':False,
        'CPU_colour_framebuffer_mapped':False,'shader_caps':0,'physical_hardware_tested':False,
        'full_2D_complete':False,
        'xorg_server_configuration':{'COMPOSITE':server_composite,'SCREENSAVER':server_screensaver,
            'header_sha256':sha(sysroot/'usr/include/xorg/xorg-server.h'),
            'feature_macros_forced':False},
        'new_2D_features':['colour-source CopyPlane to native colour and owned source bitmap',
            'basic Porter-Duff operations on opaque RGB destinations',
            'premultiplied translucent solid sources and exact A8 blending',
            'exact nearest/bilinear/convolution affine/projective colour with repeat modes',
            'native RGBX/RGB555 red/blue channel orders and inter-format conversion',
            'depth-32 core pixmaps with all Boolean ALUs and alpha plane masks',
            'typed owned CT32 RGBA copy and bounded source-alpha RGB multiplication',
            'varying ARGB alpha, thirteen basic operators and exact nearest/bilinear/convolution',
            'native a8b8g8r8 advertisement alongside a8r8g8b8 and default formats',
            'BGRA/BGRX low-byte alpha and depth-32 RGBX pictures with all three filters',
            'A1/A8 sources to RGB/ARGB through GS, and RGB/ARGB alpha to A1/A8 destinations',
            'owned A1/A8 masks with nearest/bilinear/convolution coverage',
            'GS-only alpha and packed A1/A8 decode/filter/blend/encode arithmetic',
            'component-alpha colour and A1/A8 masks with thirteen basic operators and three filters',
            'A1/A8 core Boolean drawing, planar/packed uploads and GS-masked replies',
            'A8 CopyPlane to RGB555/RGBX/raw32 with GS decode and pixel selection',
            'RGB444 in both orders with GS decode/encode and all compositor filters',
            'A4 byte coverage sources, targets, masks, core drawing and transfers',
            'RGB332/BGR233 and ARGB2222/ABGR2222 with GS colour/alpha conversion',
            'depth-16 raw core drawing, uploads, masked replies and all CopyPlane bits',
            'RGB565, ARGB1555, ARGB4444 and depth-16 RGB555/RGB444 in both orders',
            'GS alpha maps on sources, masks and destinations before filtering, with packed A1/A4/A8 replies',
            'all-depth CPU raster access rejection; unsupported work fails closed',
            'GS binary32 nearest and full-16-fraction bilinear Saturate on UNORM1..8 packed RGB/ARGB with scalar and component-alpha masks',
            'GS Saturate to A1/A4/A8 coverage with bilinear and component-alpha inputs',
            'original RGBA16 solid sources and scalar/component-alpha solid masks in GS Saturate',
            'nearest/bilinear Saturate with original-precision source/mask alpha maps and independently quantized writable maps',
            'GS scanline publication and per-store packed precision for shifted same-format writable alpha-map aliases',
            'GS wide signed convolution Saturate with the R5900 per-tap float/unsigned conversion and final signed clamp',
            '128 bounded GS surface handles for packed codecs and arithmetic workspaces',
            'native depth-15 differing RGB555/RGB444 writable map views with GS raw-word synchronization',
            'writable map aliases resolved by validated backing pixmaps for windows and distinct pixmap headers',
            'GS-only XRender Screen with exact byte multiplication, scalar/component-alpha masks, coverage and writable maps',
            'GS-only XRender Exclusion with two unrounded integer products and one final normalization',
            'GS-only XRender Multiply with exact separately rounded products and component alpha',
            'GS-only Darken, Lighten and Difference with exact 17-bit sums, product comparison and final normalization',
            'GS-only Overlay and HardLight with unrounded signed products and per-channel branch selection',
            'GS-only disjoint/conjoint Clear/Src/Dst and wide DisjointOverReverse with RGBA16 solids, maps and all canonical filters',
            'all disjoint/conjoint XRender operators with GS binary32 ratio factors and original-precision sampling',
            'GS-only binary32 ColorDodge and ColorBurn with per-channel comparisons and zero branches',
            'GS-only SoftLight and HSL blends with original-precision solids and gradient sources/masks',
            'all53 defined Render operator IDs on four depth32 ten-bit layouts, nearest/bilinear/convolution, scalar/component-alpha masks and raw source/mask snapshots',
            'original RGB10/A2 convolution with signed GS taps, R5900 per-tap unsigned conversion and kernel/geometry/work-budget preflight',
            'GS source/mask alpha-map replacement before ten-bit nearest/bilinear/convolution sampling, raw A2 maps and shared raw alias snapshots',
            'GS ten-bit writable destination maps with independent format quantization, destination-alpha scanline staging and parent-before-map publication',
            'mixed ten-bit/coverage/byte/word/native15 colour with original-precision GS codecs and parent/map alias store order',
            'native depth30 XRGB/XBGR10 pixmaps, RGB8 views and 30-bit core plane masks',
            'all38 advertised Direct formats and all six server filter names: nearest, bilinear, convolution, fast, good and best',
            'bounded native CT16 texel and scanline conversion, immutable aliases and GS display capture/output staging',
            'healthy VRAM and handle exhaustion reject allocation without poisoning later GS commands; hardware/protocol failures remain sticky',
            'core text ALUs, plane masks and tiled fills'],
        'remaining_2D':['full current-module native and desktop qualification',
            'physical and memory/performance qualification'],
        'diagnostic_logging':diagnostics}
    (work/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('work','sysroot','host-tools','staging'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--toolchain-prefix',required=True);parser.add_argument('--jobs',type=int,default=2)
    parser.add_argument('--diagnostics',action='store_true')
    args=parser.parse_args();build(args.work,args.toolchain_prefix,args.sysroot,args.host_tools,args.jobs,args.staging,args.diagnostics)

if __name__=='__main__':main()
