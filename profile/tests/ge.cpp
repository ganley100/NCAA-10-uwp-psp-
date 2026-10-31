// Execute real GE display lists and inspect their pixels in both backends.
#include "ge.hpp"
#include "ge_math.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <array>
#include <bit>
#include <cassert>
#include <iostream>

namespace {
void float24_rows() {
    using pspweb::ge_math::dot;
    auto bits = [](float value) { return std::bit_cast<std::uint32_t>(value); };
    // Alignment drops the small positive term before the large terms cancel.
    assert(bits(dot({32768, -32768, .75f, 0}, {1, 1, 1, 0})) == 0);
    assert(bits(dot({-32768, 32768, -.75f, 0}, {1, 1, 1, 0})) == 0);
    // Vertex inputs lose their low eight mantissa bits before multiplication.
    assert(bits(dot({std::bit_cast<float>(0x3F8000FFu), 0, 0, 0}, {1, 0, 0, 0})) == 0x3F800000u);
    // Represented subnormal output survives this stage, but not its next input.
    const float sub = dot({0x1p-64f, 0, 0, 0}, {0x1p-64f, 0, 0, 0});
    assert(bits(sub) == 0x00200000u);
    assert(bits(dot({sub, 0, 0, 0}, {0x1p64f, 0, 0, 0})) == 0);
    assert(bits(dot({0x1p100f, 0, 0, 0}, {0x1p100f, 0, 0, 0})) == 0x7F800000u);
    assert(bits(pspweb::ge_math::scale_offset(1.5f, 1 + 0x1p-15f, -1.5f)) == 0x38000000u);
}

void viewport_arithmetic() {
    using namespace pspweb::ge_math;
    auto bits = [](float value) { return std::bit_cast<std::uint32_t>(value); };
    // Reference literals cover exact powers, segment edges and both ends.
    for (const auto &pair : std::array<std::array<unsigned, 2>, 6>{{
        {0x3F800000, 0x3F800000}, {0x3F800100, 0x3F7FFE00}, {0x3F80FF00, 0x3F7E0600},
        {0x3F810000, 0x3F7E0300}, {0x3FC00000, 0x3F2AAA00}, {0x3FFFFF00, 0x3F000100}}}) {
        assert(bits(reciprocal(std::bit_cast<float>(pair[0]))) == pair[1]);
        assert(bits(reciprocal(std::bit_cast<float>(pair[0] | 0x80000000u))) == (pair[1] | 0x80000000u));
    }
    // Addition can keep a carry bit that a row result would discard.
    assert(bits(aligned_add(1.5f, .5f + 0x1p-15f)) == 0x40000080u);
    assert(aligned_add(32768, -32767.5f) == 1);
    assert(bits(product24(1.5f, 1 + 0x1p-15f)) == 0x3FC00100u);
    assert(viewport(.016875f, 1, 32, 2048, 1) == 2048.5f);
    const float w = std::bit_cast<float>(0x3FA66600u);
    assert(std::floor(viewport(.75f, w, 32767, 32767, reciprocal(w))) == 51670);
    assert(std::floor(viewport(-.25f, w, 32767, 32767, reciprocal(w))) == 26466);
}

constexpr std::uint32_t list = 0x08800000u, vertices = 0x08810000u;
constexpr std::uint32_t sample = 0x04000000u + (32u * 64u + 32u) * 4u;
class DisplayList {
public:
    explicit DisplayList(psprecomp::GuestMemory &memory) : memory_(memory) {}
    void command(std::uint32_t op, std::uint32_t data) {
        memory_.store32(list + size_++ * 4u, (op << 24u) | (data & 0xFFFFFFu));
    }
    void number(std::uint32_t op, float value) { command(op, std::bit_cast<std::uint32_t>(value) >> 8u); }
    void draw(unsigned type = 3, unsigned count = 3) {
        command(0x10, 0x080000u);
        command(0x01, vertices & 0xFFFFFFu);
        command(0x04, (type << 16u) | count);
    }
    void run(pspweb::Ge &ge) { command(0x0C, 0u); assert(ge.run_list(list, 0u)); }
private:
    psprecomp::GuestMemory &memory_;
    std::uint32_t size_{};
};

void expect_pixel(psprecomp::GuestMemory &memory, pspweb::GeGl *gl, std::uint32_t rgb, unsigned tolerance = 0,
                  std::uint32_t address = sample) {
    if (gl != nullptr) {
        gl->flush();
        // A prior pixel read can already have synchronized this whole surface.
        gl->prepare_vram_read(address, 4u);
        assert(glGetError() == GL_NO_ERROR);
    }
    const auto actual = memory.load32(address) & 0xFFFFFFu;
    bool matches = true;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        const int delta = static_cast<int>((actual >> shift) & 255) - static_cast<int>((rgb >> shift) & 255);
        matches &= std::abs(delta) <= static_cast<int>(tolerance);
    }
    if (!matches) std::cerr << "pixel: expected 0x" << std::hex << rgb << ", got 0x" << actual << std::dec << '\n';
    assert(matches);
}

void viewport_pixels(bool use_gl) {
    std::cout << "GE reciprocal/viewport pixels: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory; pspweb::GeGl gl(memory); pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    auto *backend = use_gl ? &gl : nullptr;
    auto geometry = [&](const std::array<float, 9> &values) {
        if (backend) backend->flush();
        for (unsigned i = 0; i < values.size(); ++i)
            memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(values[i]));
    };
    geometry({.016875f, -.75f, 0, .016875f, .75f, 0, .75f, 0, 0});
    DisplayList setup(memory);
    setup.command(0x9C, 0); setup.command(0x9D, 64); setup.command(0xD2, 3);
    setup.command(0x9E, 0x100000); setup.command(0x9F, 64);
    setup.command(0xD4, 0); setup.command(0xD5, 63 | (63 << 10));
    setup.command(0x12, 3u << 7);
    setup.number(0x42, 32); setup.number(0x43, 32); setup.number(0x44, 32767);
    setup.number(0x45, 2048); setup.number(0x46, 2048); setup.number(0x47, 32767);
    setup.command(0x4C, 2016 * 16); setup.command(0x4D, 2016 * 16);
    setup.command(0x55, 0xFFFFFF); setup.command(0x58, 255);
    setup.draw(); setup.run(ge);
    // Add at screen center 2048 before offset removal: left edge is 32.5.
    // Removing the center early and applying the old bias put it at 32.5625.
    expect_pixel(memory, backend, 0xFFFFFF);
    for (const auto &pair : std::array<std::array<float, 2>, 2>{{{.75f, 51670}, {-.25f, 26466}}}) {
        geometry({-.75f, -.75f, pair[0], .75f, -.75f, pair[0], 0, .75f, pair[0]});
        DisplayList first(memory);
        first.command(0x12, 3u << 7); first.command(0x3E, 0);
        for (unsigned i = 0; i < 16; ++i) first.number(0x3F, i == 15 ? 1.3f : (i % 5 == 0 ? 1.0f : 0.0f));
        first.command(0x23, 1); first.command(0xE7, 0); first.command(0xDE, 1);
        first.command(0x55, 0x0000FF); first.draw(); first.run(ge);
        expect_pixel(memory, backend, 0x0000FF);
        geometry({8, 8, pair[1], 56, 8, pair[1], 32, 56, pair[1]});
        DisplayList equal(memory);
        equal.command(0x12, (3u << 7) | (1u << 23)); equal.command(0xDE, 2);
        equal.command(0x55, 0x00FF00); equal.draw(); equal.run(ge);
        // Exact through depth agrees with transformed depth, unlike IEEE divide.
        expect_pixel(memory, backend, 0x00FF00);
    }
}

void float24_matrix_composition(bool use_gl) {
    std::cout << "GE float24 matrix composition: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory; pspweb::GeGl gl(memory); pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    auto *backend = use_gl ? &gl : nullptr;
    const std::array<float, 9> triangle = {-.5f, -.25f, 0, 0, .25f, 0, .5f, -.25f, 0};
    for (unsigned i = 0; i < triangle.size(); ++i)
        memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(triangle[i]));
    DisplayList setup(memory);
    setup.command(0x9C, 0); setup.command(0x9D, 64); setup.command(0xD2, 3);
    setup.command(0xD4, 0); setup.command(0xD5, 63 | (63 << 10));
    setup.command(0x12, 3u << 7);
    setup.number(0x42, 32); setup.number(0x43, 32); setup.number(0x44, 32767);
    setup.number(0x45, 32); setup.number(0x46, 32); setup.number(0x47, 32767);
    setup.command(0x55, 0xFFFFFF); setup.command(0x58, 255);
    setup.command(0x3A, 0);
    for (float v : std::array<float, 12>{1, 0, 0, 0, 1, 0, 0, 0, 1, 32768, 32768, 0}) setup.number(0x3B, v);
    setup.command(0x3C, 0);
    for (float v : std::array<float, 12>{1, 0, 0, -1, 1, 0, 0, 0, 1, .75f, -32768, 0}) setup.number(0x3D, v);
    setup.command(0x3E, 0);
    for (unsigned i = 0; i < 16; ++i) setup.number(0x3F, i == 0 ? 1.5f : (i % 5 == 0 ? 1.0f : 0.0f));
    setup.draw(); setup.run(ge);
    // P * (V * W) drops .75 before cancellation, centering the draw.
    // Grouping (P * V) * W instead keeps an erroneous +1 translation.
    expect_pixel(memory, backend, 0xFFFFFF);
    // A lit draw uses the same combined clip matrix, not rounded world positions.
    memory.store32(sample, 0);
    if (backend) ge.touch_vram(sample, 4);
    DisplayList lit(memory); lit.command(0x17, 1); lit.command(0x5C, 0xFFFFFF);
    lit.command(0x5D, 255); lit.draw(); lit.run(ge);
    expect_pixel(memory, backend, 0xFFFFFF);
}

void float24_uv_stages(bool use_gl) {
    std::cout << "GE float24 UV stages: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory; pspweb::GeGl gl(memory); pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    auto *backend = use_gl ? &gl : nullptr;
    constexpr auto texture = vertices + 0x2000;
    memory.store32(texture, 0xFF0000FF); memory.store32(texture + 4, 0xFF00FF00);
    const std::array<float, 15> triangle = {1.5f, 0, -.75f, -.75f, 0,
                                          1.5f, 0, .75f, -.75f, 0,
                                          1.5f, 0, 0, .75f, 0};
    for (unsigned i = 0; i < triangle.size(); ++i)
        memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(triangle[i]));
    DisplayList setup(memory);
    setup.command(0x9C, 0); setup.command(0x9D, 64); setup.command(0xD2, 3);
    setup.command(0xD4, 0); setup.command(0xD5, 63 | (63 << 10));
    setup.command(0x12, 3u | (3u << 7));
    setup.number(0x42, 32); setup.number(0x43, 32); setup.number(0x44, 32767);
    setup.number(0x45, 32); setup.number(0x46, 32); setup.number(0x47, 32767);
    setup.command(0x55, 0xFFFFFF); setup.command(0x58, 255);
    setup.command(0xA0, texture & 0xFFFFFF); setup.command(0xA8, 0x080200);
    setup.command(0xB8, 9); setup.command(0xC3, 3); setup.command(0x1E, 1);
    setup.command(0xC7, 0x0101); setup.command(0xC9, 3);
    setup.number(0x48, -1 - 0x1p-15f); setup.number(0x49, 1);
    setup.number(0x4A, 1.5f + 65 * 0x1p-15f);
    setup.draw(); setup.run(ge);
    // Separate product truncation selects texel 1; an IEEE expression selects 0.
    expect_pixel(memory, backend, 0x00FF00);
    DisplayList lit(memory); lit.command(0x17, 1); lit.command(0x5C, 0xFFFFFF);
    lit.command(0x5D, 255); lit.draw(); lit.run(ge);
    expect_pixel(memory, backend, 0x00FF00);
}

void texture_row_alignment(bool use_gl) {
    std::cout << "GE texture row alignment: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory; pspweb::GeGl gl(memory); pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    constexpr unsigned texture=vertices+0x2000, clut=texture+0x4000;
    constexpr std::array<unsigned,11> minimum={8,8,8,4,32,16,8,4,32,16,16};
    for (unsigned i=0;i<8;++i) memory.store32(clut+i*4,i==1?0xFF0000FF:i==2?0xFFFF0000:0xFF000000);
    for (unsigned format=0;format<11;++format) for (unsigned test=0;test<4;++test) {
        const unsigned row=minimum[format]*(test==3?2:1);
        const std::array<unsigned,4> encoded={0,minimum[format]-1,minimum[format]+1,2*minimum[format]+1};
        if (format<8) {
            auto pixel=[&](unsigned i,bool red) {
                switch (format) {
                case 0:memory.store16(texture+i*2,red?0x001F:0xF800);break;
                case 1:memory.store16(texture+i*2,red?0x801F:0xFC00);break;
                case 2:memory.store16(texture+i*2,red?0xF00F:0xFF00);break;
                case 3:memory.store32(texture+i*4,red?0xFF0000FF:0xFFFF0000);break;
                case 4: {
                    const unsigned address=texture+i/2, shift=(i%2)*4;
                    memory.store8(address,(memory.load8(address)&~(15u<<shift))|((red?1u:2u)<<shift));break;
                }
                case 5:memory.store8(texture+i,red?1:2);break;
                case 6:memory.store16(texture+i*2,red?1:2);break;
                case 7:memory.store32(texture+i*4,red?1:2);break;
                }
            };
            for (unsigned i=0;i<row*2;++i) pixel(i,false);
            pixel(row,true);
        } else {
            const unsigned block_bytes=format==8?8:16, blocks=row/4;
            for (unsigned i=0;i<blocks*2;++i) {
                const unsigned address=texture+i*block_bytes;
                memory.store32(address,0); memory.store16(address+4,i==blocks?0xF800:0x001F); memory.store16(address+6,0);
                if (format==9) {memory.store32(address+8,0xFFFFFFFF);memory.store32(address+12,0xFFFFFFFF);}
                if (format==10) {memory.store32(address+8,0);memory.store16(address+12,0);memory.store8(address+14,255);memory.store8(address+15,255);}
            }
        }
        ge.next_frame(); // Exercise revalidation when row padding/content changes.
        const std::array<float,10> data={.5f,format<8?1.5f:4.5f,24,24,0,.5f,format<8?1.5f:4.5f,40,40,0};
        for (unsigned i=0;i<data.size();++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(data[i]));
        DisplayList dl(memory);
        dl.command(0x9C,0);dl.command(0x9D,64);dl.command(0xD2,3);dl.command(0xD4,0);dl.command(0xD5,63|(63<<10));
        dl.command(0x12,3u|(3u<<7)|(1u<<23));dl.command(0x1E,1);dl.command(0xD3,0);dl.command(0x21,0);dl.command(0x22,0);
        dl.command(0xA0,texture&0xFFFFFF);dl.command(0xA8,0x080000|encoded[test]);dl.command(0xB8,format<8?0x101:0x302);
        dl.command(0xC2,0);dl.command(0xC3,format);dl.command(0xC6,0);dl.command(0xC7,0x101);dl.command(0xC8,1);dl.command(0xC9,0x103);
        if (format>=4 && format<=7) {dl.command(0xB0,clut&0xFFFFFF);dl.command(0xB1,0x080000);dl.command(0xC5,0xFF03);dl.command(0xC4,1);}
        const auto downloads=gl.downloads();dl.draw(6,2);dl.run(ge);
        if (use_gl) {gl.flush();assert(gl.downloads()==downloads);}
        expect_pixel(memory,use_gl?&gl:nullptr,255);
    }
}

void staged_bilinear_precision() {
    std::cout << "GE staged bilinear truncation: GLES3" << std::endl;
    psprecomp::GuestMemory memory; pspweb::GeGl gl(memory); pspweb::Ge ge(memory);
    assert(gl.init());ge.set_gl(&gl);
    constexpr unsigned texture=vertices+0x2000;
    const std::array<unsigned,4> colors={0x1E0A141E,0x1F0B151F,0x1F0B151F,0x200C1620};
    for (unsigned i=0;i<4;++i) memory.store32(texture+((i/2)*4+i%2)*4,colors[i]);
    auto clear=[&](unsigned offset,unsigned color,float x,float y,float end_x,float end_y) {
        const std::array<float,6> data={x,y,0,end_x,end_y,0};
        for (unsigned i=0;i<data.size();++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(data[i]));
        DisplayList dl(memory);dl.command(0x9C,offset);dl.command(0x9D,64);dl.command(0xD2,3);
        dl.command(0xD4,0);dl.command(0xD5,63|(63<<10));dl.command(0x12,(3u<<7)|(1u<<23));
        dl.command(0x1E,0);dl.command(0x21,0);dl.command(0x22,0);dl.command(0x24,0);dl.command(0xD3,0x301);
        dl.command(0x55,color&0xFFFFFF);dl.command(0x58,color>>24);dl.draw(6,2);dl.run(ge);
    };
    for (int scale:{1,2}) for (bool gpu_source:{false,true}) {
        assert(gl.set_render_scale(scale)==scale);
        if (gpu_source) for (unsigned i=0;i<4;++i)
            clear(0,colors[i],8+(i%2),8+(i/2),9+(i%2),9+(i/2));
        clear(0x60000,0xFFFF00FF,24,24,40,40);
        const unsigned source=gpu_source?0x04000000+(8*64+8)*4:texture;
        // At 2x a GPU window retains physical taps; choose the same four weights.
        const float uv=1.0f;
        const std::array<float,10> data={uv,uv,24,24,0,uv,uv,40,40,0};
        for (unsigned i=0;i<data.size();++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(data[i]));
        DisplayList dl(memory);dl.command(0xD3,0);dl.command(0x12,3u|(3u<<7)|(1u<<23));dl.command(0x1E,1);
        // Encoded GPU stride 65 must alias the existing 64-pixel GPU surface.
        dl.command(0xA0,source&0xFFFFFF);dl.command(0xA8,gpu_source?0x040041:0x080004);dl.command(0xB8,0x101);
        dl.command(0xC2,0);dl.command(0xC3,3);dl.command(0xC6,0x101);dl.command(0xC7,0x101);dl.command(0xC8,1);dl.command(0xC9,0x103);
        dl.command(0x22,1);dl.command(0xDB,2|(30<<8)|0xFF0000);
        const auto downloads=gl.downloads();dl.draw(6,2);dl.run(ge);gl.flush();assert(gl.downloads()==downloads);
        // Horizontal averages 30.5/31.5 truncate to 30/31, then to 30 vertically.
        // Combining all four products would yield alpha/RGB 31 and fail the test.
        expect_pixel(memory,&gl,0x0A141E,0,0x04060000+(32*64+32)*4);
    }
}

void affine_color_perspective_uv() {
    std::cout << "Affine color and explicit perspective UV: GLES3" << std::endl;
    psprecomp::GuestMemory memory;pspweb::GeGl gl(memory);assert(gl.init());
    pspweb::GlDrawState state{};state.fb_stride=64;state.fb_format=3;state.sx2=state.sy2=63;
    gl.set_state(state);
    auto vertex=[](float x,float y,float w,float u,float v) {
        pspweb::GlVertex p{};p.x=x;p.y=y;p.w=w;p.u=u;p.v=v;p.r=41;p.g=73;p.b=109;p.a=255;return p;
    };
    const auto a=vertex(8,8,1.3f,0,0),b=vertex(16,8,3.7f,8,0),c=vertex(8,16,9.1f,0,8);
    gl.triangle(a,b,c);gl.flush();assert(gl.downloads()==0);
    for (unsigned y=8;y<15;++y) for (unsigned x=8;x+y<23;++x)
        expect_pixel(memory,&gl,0x6D4929,0,0x04000000+(y*64+x)*4);
    std::array<unsigned,64> texels{};
    for (unsigned y=0;y<8;++y) for (unsigned x=0;x<8;++x) texels[y*8+x]=0xFF000000|(x*30)|((y*30)<<8);
    state.texture=true;state.tex_func=3;state.tex_alpha=true;state.texels=texels.data();state.texture_identity=texels.data();
    state.tex_width=state.tex_height=8;state.lod_mode=1;gl.set_state(state);
    gl.triangle(vertex(8,8,1,0,0),vertex(16,8,2,8,0),vertex(8,16,4,0,8));
    expect_pixel(memory,&gl,0x001E3C,0,0x04000000+(10*64+10)*4);
    // Texture color doubling includes secondary light, but is inactive when
    // texturing is disabled even if the previous texture state enabled it.
    texels[0]=0xFF000001;state.tex_width=state.tex_height=1;state.texture_hash=1;state.tex_double=true;gl.set_state(state);
    auto specular=[&](float x,float y) {auto p=vertex(x,y,1,0,0);p.r=p.g=p.b=0;p.sr=8;return p;};
    gl.triangle(specular(8,8),specular(16,8),specular(8,16));
    expect_pixel(memory,&gl,18,0,0x04000000+(10*64+10)*4);
    state.texture=false;gl.set_state(state);gl.triangle(specular(8,8),specular(16,8),specular(8,16));
    expect_pixel(memory,&gl,8,0,0x04000000+(10*64+10)*4);
}

void triangle_sampling(bool use_gl) {
    std::cout << "Triangle grid/center and rectangle sampling: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory); pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    auto clear = [&]() {
        const std::array<float,6> data={0,0,0,64,64,0};
        for (unsigned i=0;i<data.size();++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(data[i]));
        DisplayList dl(memory);
        dl.command(0x9C,0); dl.command(0x9D,64); dl.command(0xD2,3);
        dl.command(0xD4,0); dl.command(0xD5,63|(63<<10));
        dl.command(0x12,(3u<<7)|(1u<<23)); dl.command(0x1D,0); dl.command(0x1E,0);
        dl.command(0x21,0); dl.command(0x22,0); dl.command(0x23,0); dl.command(0xD3,0x301);
        dl.command(0x55,0); dl.command(0x58,0); dl.draw(6,2); dl.run(ge);
    };
    // Through coordinates truncate to 1/16. A .03 displacement snaps to zero;
    // a displacement of .0625 moves by exactly one subpixel.
    for (float offset : {0.0f,.03f,.0625f}) {
        clear();
        struct V { unsigned color; float x,y,z; };
        const std::array<V,3> data={{{0xFF000000,8+offset,8,0},{0xFF0000FF,16+offset,8,0},{0xFF000000,8+offset,16,0}}};
        for (unsigned i=0;i<data.size();++i) {
            memory.store32(vertices+i*16,data[i].color);
            memory.store32(vertices+i*16+4,std::bit_cast<unsigned>(data[i].x));
            memory.store32(vertices+i*16+8,std::bit_cast<unsigned>(data[i].y)); memory.store32(vertices+i*16+12,0);
        }
        DisplayList dl(memory);
        dl.command(0xD3,0); dl.command(0x12,(7u<<2)|(3u<<7)|(1u<<23)); dl.command(0x50,1);
        const auto downloads=gl.downloads(); dl.draw(3,3); dl.run(ge);
        if (use_gl) { gl.flush(); assert(gl.downloads()==downloads); }
        // Hardware oracle: floor(255 * .5 / 8) = 15; one subpixel gives 13.
        expect_pixel(memory,use_gl?&gl:nullptr,offset==.0625f?13:15,0,0x04000000+(8*64+8)*4);
    }
    // Flat axis-aligned strips/fans retain rectangle half-pixel UV sampling.
    // The half-pixel sample u=1 selects texel 1 in this exact-power-of-two fixture.
    constexpr unsigned texture=vertices+0x1000;
    for (unsigned x=0;x<8;++x) memory.store32(texture+x*4,0xFF000000|x*30);
    for (unsigned test : {0u,1u,2u,3u}) {
        const unsigned primitive = test == 1 || test == 2 ? 5u : 4u;
        const bool rectangle = test == 1 || test == 3;
        clear();
        const std::array<std::array<float,5>,4> corners={{{.5f,.5f,8,8,0},{8.5f,.5f,16,8,0},
                                                       {.5f,test==1||test==3?8.5f:.5f,8,16,0},{8.5f,test==1||test==3?8.5f:.5f,16,16,0}}};
        const std::array<unsigned,4> order=primitive==4?std::array<unsigned,4>{0,1,2,3}:std::array<unsigned,4>{0,1,3,2};
        for (unsigned i=0;i<4;++i) for (unsigned j=0;j<5;++j)
            memory.store32(vertices+(i*5+j)*4,std::bit_cast<unsigned>(corners[order[i]][j]));
        DisplayList dl(memory);
        dl.command(0xD3,0); dl.command(0x12,3u|(3u<<7)|(1u<<23)|(test==3?2u<<11:0u)); dl.command(0x1E,1);
        if (test == 3) {
            constexpr unsigned indices = texture + 0x1000;
            for (unsigned i=0;i<4;++i) memory.store16(indices+i*2,(i+2)%4);
            dl.command(0x02,indices&0xFFFFFF);
        }
        dl.command(0xA0,texture&0xFFFFFF); dl.command(0xA8,0x080008); dl.command(0xB8,3);
        dl.command(0xC2,0); dl.command(0xC3,3); dl.command(0xC6,0); dl.command(0xC8,1);
        dl.command(0xC9,0x103); dl.command(0x55,0xFFFFFF); dl.command(0x58,255);
        const auto before=ge.stats().sprites, downloads=gl.downloads(); dl.draw(primitive,4); dl.run(ge);
        if (use_gl) { gl.flush(); assert(gl.downloads()==downloads); }
        // A constant-V fan is conservatively declined; indexed/reversed strips
        // and a fan with separable positive UV ranges use the rectangle path.
        assert(ge.stats().sprites==before+(rectangle?1u:0u));
        expect_pixel(memory,use_gl?&gl:nullptr,30,0,0x04000000+(8*64+8)*4);
    }
    // Four corners alone do not make a rectangle: the shared edge must be a
    // diagonal. This strip has a gap above and two overlapping triangles below.
    clear();
    const std::array<float,12> malformed={8,8,0,8,16,0,16,16,0,16,8,0};
    for (unsigned i=0;i<malformed.size();++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(malformed[i]));
    DisplayList dl(memory);
    dl.command(0xD3,0); dl.command(0x55,255); dl.command(0x58,128); dl.command(0x21,1); dl.command(0xDF,0x32);
    const auto before=ge.stats().sprites; dl.draw(4,4); dl.run(ge);
    assert(ge.stats().sprites==before);
    expect_pixel(memory,use_gl?&gl:nullptr,0,0,0x04000000+(9*64+10)*4);
    // The separately truncated blend products are 128 + 63, not 192.
    expect_pixel(memory,use_gl?&gl:nullptr,191,0,0x04000000+(14*64+10)*4);
    // Two adjacent rectangles in one strip must consume exactly two triangles
    // each and blend the shared row once. Exercise both internal resolutions.
    for (int scale : {1,2}) {
        if (!use_gl && scale==2) continue;
        if (use_gl) assert(gl.set_render_scale(scale)==scale);
        clear();
        const std::array<float,18> chain={8,8,0,16,8,0,8,16,0,16,16,0,8,24,0,16,24,0};
        for (unsigned i=0;i<chain.size();++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(chain[i]));
        DisplayList adjacent(memory);
        adjacent.command(0xD3,0); adjacent.command(0x55,255); adjacent.command(0x58,128);
        adjacent.command(0x21,1); adjacent.command(0xDF,0x32);
        const auto before=ge.stats().sprites, downloads=gl.downloads();
        adjacent.draw(4,6); adjacent.run(ge);
        if (use_gl) { gl.flush(); assert(gl.downloads()==downloads); }
        assert(ge.stats().sprites==before+2);
        for (unsigned y : {8u,15u,16u,23u})
            expect_pixel(memory,use_gl?&gl:nullptr,128,0,0x04000000+(y*64+12)*4);
        expect_pixel(memory,use_gl?&gl:nullptr,0,0,0x04000000+(24*64+12)*4);
    }
}

void line_coverage(bool use_gl) {
    std::cout << "Line list/strip coverage: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    struct Case { float x0, y0, x1, y1; std::array<std::array<unsigned, 2>, 4> pixels; unsigned count; };
    const std::array<Case, 10> cases = {{
        {8, 8, 12, 8, {{{8,8}, {9,8}, {10,8}, {11,8}}}, 4},
        {12, 8, 8, 8, {{{8,8}, {9,8}, {10,8}, {11,8}}}, 4},
        {8, 8, 8, 12, {{{8,8}, {8,9}, {8,10}, {8,11}}}, 4},
        {8, 12, 8, 8, {{{8,8}, {8,9}, {8,10}, {8,11}}}, 4},
        {8, 8, 12, 12, {{{8,8}, {9,9}, {10,10}, {11,11}}}, 4},
        {12, 12, 8, 8, {{{8,8}, {9,9}, {10,10}, {11,11}}}, 4},
        {8, 8, 10, 12, {{{8,8}, {8,9}, {9,10}, {9,11}}}, 4},
        {8.75f, 8.25f, 12.75f, 8.25f, {{{8,8}, {9,8}, {10,8}, {11,8}}}, 4},
        {8, 8, 8, 8, {}, 0}, {8, 8, 8.75f, 8, {}, 0},
    }};
    auto clear = [&]() {
        const std::array<float, 6> data = {0,0,0,64,64,0};
        for (unsigned i=0; i<data.size(); ++i) memory.store32(vertices+i*4, std::bit_cast<unsigned>(data[i]));
        DisplayList dl(memory);
        dl.command(0x9C,0); dl.command(0x9D,64); dl.command(0xD2,3);
        dl.command(0xD4,0); dl.command(0xD5,63|(63<<10));
        dl.command(0x12,(3u<<7)|(1u<<23)); dl.command(0x1E,0); dl.command(0x21,0);
        dl.command(0xD3,0x301); dl.command(0x55,0); dl.command(0x58,255);
        dl.draw(6,2); dl.run(ge);
    };
    for (int scale : {1,2}) {
        if (!use_gl && scale==2) continue;
        if (use_gl) assert(gl.set_render_scale(scale)==scale);
        for (const auto &test : cases) {
            clear();
            const std::array<float,6> data={test.x0,test.y0,0,test.x1,test.y1,0};
            for (unsigned i=0; i<data.size(); ++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(data[i]));
            DisplayList dl(memory);
            dl.command(0xD3,0); dl.command(0x55,255); dl.draw(1,2); dl.run(ge);
            if (use_gl) { const auto downloads=gl.downloads(); gl.flush(); assert(gl.downloads()==downloads); }
            for (unsigned y=4; y<16; ++y) for (unsigned x=4; x<16; ++x) {
                bool covered=false;
                for (unsigned i=0; i<test.count; ++i) covered|=test.pixels[i][0]==x && test.pixels[i][1]==y;
                expect_pixel(memory,use_gl?&gl:nullptr,covered?255:0,0,0x04000000+(y*64+x)*4);
            }
        }
        clear();
        const std::array<float,9> strip={8,8,0,12,8,0,12,12,0};
        for (unsigned i=0; i<strip.size(); ++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(strip[i]));
        DisplayList dl(memory);
        dl.command(0xD3,0); dl.command(0x55,255); dl.command(0x58,128);
        dl.command(0x21,1); dl.command(0xDF,0x32); dl.draw(2,3); dl.run(ge);
        for (unsigned y=7; y<14; ++y) for (unsigned x=7; x<14; ++x) {
            const bool covered=(y==8 && x>=8 && x<12)||(x==12 && y>=8 && y<12);
            expect_pixel(memory,use_gl?&gl:nullptr,covered?128:0,0,0x04000000+(y*64+x)*4);
        }
    }
}

void line_attributes(bool use_gl) {
    std::cout << "Line attributes, depth and clipping: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    auto clear = [&](float z=0) {
        const std::array<float,6> data={0,0,z,64,64,z};
        for (unsigned i=0; i<data.size(); ++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(data[i]));
        DisplayList dl(memory);
        dl.command(0x9C,0); dl.command(0x9D,64); dl.command(0xD2,3);
        dl.command(0x9E,0x100000); dl.command(0x9F,64);
        dl.command(0xD4,0); dl.command(0xD5,63|(63<<10));
        dl.command(0x12,(3u<<7)|(1u<<23)); dl.command(0x1E,0); dl.command(0x21,0);
        dl.command(0x22,0); dl.command(0x23,0); dl.command(0xD3,0x701);
        dl.command(0x55,0); dl.command(0x58,255); dl.draw(6,2); dl.run(ge);
    };
    for (bool smooth : {true,false}) {
        clear();
        const std::array<unsigned,8> data={0xFF000000,std::bit_cast<unsigned>(8.0f),std::bit_cast<unsigned>(8.0f),0,
                                         0xFF0000FF,std::bit_cast<unsigned>(12.0f),std::bit_cast<unsigned>(8.0f),0};
        for (unsigned i=0; i<data.size(); ++i) memory.store32(vertices+i*4,data[i]);
        DisplayList dl(memory);
        dl.command(0xD3,0); dl.command(0x12,(7u<<2)|(3u<<7)|(1u<<23)); dl.command(0x50,smooth?1:0);
        dl.draw(1,2); dl.run(ge);
        const std::array<unsigned,4> expected={0,63,127,191};
        for (unsigned x=0; x<4; ++x) expect_pixel(memory,use_gl?&gl:nullptr,smooth?expected[x]:255,0,0x04000000+(8*64+8+x)*4);
    }
    clear();
    memory.store32(vertices,0x00FFFFFF); memory.store32(vertices+16,0xFFFFFFFF);
    memory.store32(vertices+4,std::bit_cast<unsigned>(8.0f)); memory.store32(vertices+8,std::bit_cast<unsigned>(8.0f)); memory.store32(vertices+12,0);
    memory.store32(vertices+20,std::bit_cast<unsigned>(12.0f)); memory.store32(vertices+24,std::bit_cast<unsigned>(8.0f)); memory.store32(vertices+28,0);
    DisplayList alpha(memory);
    alpha.command(0xD3,0); alpha.command(0x12,(7u<<2)|(3u<<7)|(1u<<23)); alpha.command(0x50,1);
    alpha.command(0x22,1); alpha.command(0xDB,2|(127<<8)|0xFF0000); alpha.draw(1,2); alpha.run(ge);
    for (unsigned x=0; x<4; ++x) expect_pixel(memory,use_gl?&gl:nullptr,x==2?0xFFFFFF:0,0,0x04000000+(8*64+8+x)*4);

    clear(50);
    const std::array<float,6> zline={8,8,0,12,8,100};
    for (unsigned i=0; i<zline.size(); ++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(zline[i]));
    DisplayList depth(memory);
    depth.command(0xD3,0); depth.command(0x12,(3u<<7)|(1u<<23)); depth.command(0x55,255);
    depth.command(0x23,1); depth.command(0xDE,4); depth.command(0xE7,0); depth.draw(1,2); depth.run(ge);
    for (unsigned x=0; x<4; ++x) expect_pixel(memory,use_gl?&gl:nullptr,x<2?255:0,0,0x04000000+(8*64+8+x)*4);
    memory.store32(vertices+8,std::bit_cast<unsigned>(20.0f)); memory.store32(vertices+20,std::bit_cast<unsigned>(20.0f));
    DisplayList green(memory); green.command(0x55,0x00FF00); green.draw(1,2); green.run(ge);
    for (unsigned x=0; x<4; ++x) expect_pixel(memory,use_gl?&gl:nullptr,x==0?255:0x00FF00,0,0x04000000+(8*64+8+x)*4);

    clear();
    const std::array<float,9> indexed={8,8,0,12,8,0,32,32,0};
    for (unsigned i=0; i<indexed.size(); ++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(indexed[i]));
    constexpr unsigned indices=vertices+0x1000;
    memory.store16(indices,1); memory.store16(indices+2,0); memory.store16(indices+4,2);
    DisplayList index(memory);
    index.command(0xD3,0); index.command(0x55,255); index.command(0x12,(3u<<7)|(2u<<11)|(1u<<23));
    index.command(0x02,indices&0xFFFFFF); index.draw(1,3); index.run(ge);
    for (unsigned x=8; x<12; ++x) expect_pixel(memory,use_gl?&gl:nullptr,255,0,0x04000000+(8*64+x)*4);
    expect_pixel(memory,use_gl?&gl:nullptr,0);
    clear();
    const std::array<float,6> wide={-1000000,8,0,1000000,8,0};
    for (unsigned i=0; i<wide.size(); ++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(wide[i]));
    DisplayList clipped(memory);
    clipped.command(0xD3,0); clipped.command(0x55,255); clipped.command(0xD4,10|(8<<10));
    clipped.command(0xD5,11|(8<<10)); clipped.draw(1,2); clipped.run(ge);
    for (unsigned x=8; x<14; ++x) expect_pixel(memory,use_gl?&gl:nullptr,x>=10&&x<=11?255:0,0,0x04000000+(8*64+x)*4);
    for (bool clamp : {false,true}) {
        clear();
        const std::array<float,6> clipped_positions={-.75f,0,-2,.75f,0,0};
        for (unsigned i=0; i<clipped_positions.size(); ++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(clipped_positions[i]));
        DisplayList dl(memory);
        dl.command(0xD3,0); dl.command(0x12,3u<<7); dl.command(0x1C,clamp?1:0); dl.command(0x55,255);
        dl.number(0x42,32); dl.number(0x43,32); dl.number(0x44,32767);
        dl.number(0x45,32); dl.number(0x46,32); dl.number(0x47,32767);
        dl.command(0x3E,0);
        for (float v:std::array<float,16>{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}) dl.number(0x3F,v);
        dl.draw(1,2); dl.run(ge);
        for (unsigned x=8; x<=56; ++x)
            expect_pixel(memory,use_gl?&gl:nullptr,clamp&&x>=32&&x<56?255:0,0,0x04000000+(32*64+x)*4);
    }
}

void line_texture_lod() {
    std::cout << "Line perspective textures and LOD: GLES3" << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory); pspweb::Ge ge(memory);
    assert(gl.init()); ge.set_gl(&gl);
    constexpr unsigned texture=vertices+0x1000;
    for (unsigned x=0; x<8; ++x) memory.store32(texture+x*4,0xFF000000|x*30);
    const std::array<float,10> perspective={0,.5f,-.75f,0,1,1,.5f,1.5f,0,2};
    for (unsigned i=0; i<perspective.size(); ++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(perspective[i]));
    DisplayList projected(memory);
    projected.command(0x9C,0); projected.command(0x9D,64); projected.command(0xD2,3);
    projected.command(0xD4,0); projected.command(0xD5,63|(63<<10)); projected.command(0x12,3u|(3u<<7));
    projected.number(0x42,32); projected.number(0x43,32); projected.number(0x44,32767);
    projected.number(0x45,32); projected.number(0x46,32); projected.number(0x47,32767);
    projected.command(0x3E,0);
    for (float v:std::array<float,16>{1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0}) projected.number(0x3F,v);
    projected.command(0x1E,1); projected.command(0xA0,texture&0xFFFFFF); projected.command(0xA8,0x080008);
    projected.command(0xB8,3); projected.command(0xC3,3); projected.command(0xC9,3); projected.command(0xC8,1);
    projected.draw(1,2); projected.run(ge);
    expect_pixel(memory,&gl,60,0,0x04000000+(32*64+32)*4);
    expect_pixel(memory,&gl,120,0,0x04000000+(32*64+44)*4);
    for (unsigned level=0; level<3; ++level) {
        const unsigned color=level==0?0x0000FF:level==1?0x00FF00:0xFF0000;
        for (unsigned x=0; x<(64u>>level); ++x) memory.store32(texture+level*0x4000+x*4,0xFF000000|color);
    }
    ge.next_frame();
    for (int scale:{1,2}) {
        assert(gl.set_render_scale(scale)==scale);
        for (unsigned mode:{0u,1u,2u}) {
            const std::array<float,10> data={0,.5f,8,8,0,96,.5f,56,8,0};
            for (unsigned i=0; i<data.size(); ++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(data[i]));
            DisplayList dl(memory);
            dl.command(0x12,3u|(3u<<7)|(1u<<23)); dl.command(0xC2,2<<16); dl.command(0xC6,4); dl.command(0xC8,mode);
            dl.number(0xD0,1);
            for (unsigned level=0; level<3; ++level) {
                dl.command(0xA0+level,(texture+level*0x4000)&0xFFFFFF); dl.command(0xA8+level,0x080000|(64>>level));
                dl.command(0xB8+level,6-level);
            }
            const auto downloads=gl.downloads(); dl.draw(1,2); dl.run(ge); gl.flush(); assert(gl.downloads()==downloads);
            expect_pixel(memory,&gl,mode==1?0x0000FF:0x00FF00,0,0x04000000+(8*64+32)*4);
        }
    }
    // Varying clip w crosses the nearest-mip boundary halfway through the
    // line. Equal-LOD runs must not merge across that change.
    assert(gl.set_render_scale(1)==1);
    const std::array<float,10> slope_positions={.5f,.5f,-.75f,0,1,.5f,.5f,1.5f,0,2};
    for (unsigned i=0; i<slope_positions.size(); ++i) memory.store32(vertices+i*4,std::bit_cast<unsigned>(slope_positions[i]));
    DisplayList slope(memory);
    slope.command(0x12,3u|(3u<<7)); slope.command(0xC8,2); slope.number(0xD0,.5f);
    slope.draw(1,2); slope.run(ge);
    for (unsigned x:{8u,31u,32u,55u})
        expect_pixel(memory,&gl,x<32?0x0000FF:0x00FF00,0,0x04000000+(32*64+x)*4);
}

void lighting_colors(bool use_gl) {
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    auto *backend = use_gl ? &gl : nullptr;
    // Float normals and positions, no vertex color: use the material registers.
    const std::array<float, 18> data = {0, 0, 1, -.8f, -.8f, 0,
                                      0, 0, 1, .8f, -.8f, 0,
                                      0, 0, 1, 0, .8f, 0};
    for (unsigned i = 0; i < data.size(); ++i) memory.store32(vertices + i * 4u, std::bit_cast<std::uint32_t>(data[i]));
    DisplayList setup(memory);
    setup.command(0x9C, 0); setup.command(0x9D, 64); setup.command(0xD2, 3);
    setup.command(0xD4, 0); setup.command(0xD5, 63 | (63 << 10));
    setup.command(0x12, (3u << 5u) | (3u << 7u));
    setup.number(0x42, 32); setup.number(0x43, 32); setup.number(0x44, 32767);
    setup.number(0x45, 32); setup.number(0x46, 32); setup.number(0x47, 32767);
    setup.command(0x17, 1);
    setup.command(0x55, 0xFFFFFF); setup.command(0x56, 0xFFFFFF);
    setup.command(0x58, 255); setup.command(0x5D, 255);
    setup.run(ge);
    for (unsigned light = 0; light < 4; ++light) {
        const unsigned ambient = 0x8F + light * 3, diffuse = ambient + 1;
        DisplayList initial(memory);
        for (unsigned i = 0; i < 4; ++i) initial.command(0x18 + i, i == light ? 1 : 0);
        initial.number(0x63 + light * 3, 0); initial.number(0x64 + light * 3, 0);
        initial.number(0x65 + light * 3, 1);
        initial.command(ambient, 0x0000FF); initial.command(diffuse, 0);
        initial.draw(); initial.run(ge);
        expect_pixel(memory, backend, 0x0000FF);

        // Change ONLY the color, leaving every other lighting register intact.
        DisplayList green(memory);
        green.command(ambient, 0x00FF00); green.draw(); green.run(ge);
        expect_pixel(memory, backend, 0x00FF00);

        DisplayList blue(memory);
        blue.command(ambient, 0); blue.command(diffuse, 0xFF0000);
        blue.draw(); blue.run(ge);
        expect_pixel(memory, backend, 0xFF0000);

        DisplayList yellow(memory);
        yellow.command(diffuse, 0x00FFFF); yellow.draw(); yellow.run(ge);
        expect_pixel(memory, backend, 0x00FFFF);
    }
}

void lighting_components(bool use_gl) {
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    auto *backend = use_gl ? &gl : nullptr;
    auto normals = [&](float x, float y, float z) {
        const std::array<float, 18> data = {x, y, z, -.8f, -.8f, 0,
                                          x, y, z, .8f, -.8f, 0,
                                          x, y, z, 0, .8f, 0};
        for (unsigned i = 0; i < data.size(); ++i) memory.store32(vertices + i * 4u, std::bit_cast<std::uint32_t>(data[i]));
    };
    normals(0, 0, 1);
    DisplayList setup(memory);
    setup.command(0x9C, 0); setup.command(0x9D, 64); setup.command(0xD2, 3);
    setup.command(0xD4, 0); setup.command(0xD5, 63 | (63 << 10));
    setup.command(0x12, (3u << 5u) | (3u << 7u));
    setup.number(0x42, 32); setup.number(0x43, 32); setup.number(0x44, 32767);
    setup.number(0x45, 32); setup.number(0x46, 32); setup.number(0x47, 32767);
    setup.command(0x17, 1); setup.command(0x18, 1);
    setup.command(0x58, 255); setup.command(0x5D, 255);
    setup.command(0x57, 0x804020); setup.number(0x5B, 2);
    setup.command(0x5F, 1); // directional, diffuse and specular
    setup.number(0x65, 1); setup.command(0x91, 0xFFFFFF);
    setup.draw(); setup.run(ge);
    expect_pixel(memory, backend, 0x804020);

    // A black texture removes combined highlights but must leave separate
    // highlights intact, because secondary RGB is added after texturing.
    constexpr std::uint32_t texture = vertices + 0x1000;
    memory.store32(texture, 0xFF000000);
    DisplayList black(memory);
    black.command(0xA0, texture & 0xFFFFFF); black.command(0xA8, 0x080001);
    black.command(0xB8, 0); black.command(0xC3, 3); black.command(0x1E, 1);
    black.draw(); black.run(ge);
    expect_pixel(memory, backend, 0);
    DisplayList separate(memory);
    separate.command(0x5E, 1); separate.draw(); separate.run(ge);
    expect_pixel(memory, backend, 0x804020);

    // Fog follows the texture operation AND separate specular addition.
    DisplayList fogged_highlight(memory);
    fogged_highlight.command(0x1F, 1); fogged_highlight.number(0xCD, .5f);
    fogged_highlight.number(0xCE, 1); fogged_highlight.command(0xCF, 0x00FF00);
    fogged_highlight.draw(); fogged_highlight.run(ge);
    expect_pixel(memory, backend, 0x409F10);
    DisplayList unfogged_highlight(memory);
    unfogged_highlight.command(0x1F, 0); unfogged_highlight.draw(); unfogged_highlight.run(ge);
    expect_pixel(memory, backend, 0x804020);

    DisplayList blue(memory);
    blue.command(0x57, 0xFFFFFF); blue.command(0x91, 0xFF0000);
    blue.draw(); blue.run(ge);
    expect_pixel(memory, backend, 0xFF0000);
    DisplayList diffuse_only(memory);
    diffuse_only.command(0x5F, 0); diffuse_only.draw(); diffuse_only.run(ge);
    expect_pixel(memory, backend, 0);

    // Vertex material tracking has no effect without a color attribute.
    DisplayList material(memory);
    material.command(0x1E, 0); material.command(0x5E, 0);
    material.command(0x55, 0x00FF00); material.command(0x56, 0x0000FF);
    material.command(0x53, 2); material.command(0x90, 0xFFFFFF);
    material.draw(); material.run(ge);
    expect_pixel(memory, backend, 0x0000FF);

    // N.L = 1/2. Powered diffuse with exponent 2 gives 1/4; the GE's
    // 8-bit lighting scale maps full white to 64, not floating-point 63.75.
    normals(0.8660254f, 0, .5f);
    DisplayList powered(memory);
    powered.command(0x53, 0); powered.command(0x56, 0xFFFFFF);
    powered.command(0x5F, 2); powered.number(0x5B, 2);
    powered.draw(); powered.run(ge);
    expect_pixel(memory, backend, 0x404040);
    // The exponent keeps four mantissa bits: 2.99 -> 2.875. The GE's
    // piecewise-linear power at 1/2 then gives .140625, an 8-bit factor of 36.
    DisplayList exponent(memory);
    exponent.number(0x5B, 2.99f); exponent.draw(); exponent.run(ge);
    expect_pixel(memory, backend, 0x242424);
    DisplayList constant(memory);
    constant.number(0x5B, 0); constant.draw(); constant.run(ge);
    expect_pixel(memory, backend, 0xFFFFFF);

    normals(0, 0, 1);
    DisplayList reverse(memory);
    reverse.command(0x5F, 1); reverse.command(0x90, 0); reverse.number(0x5B, 0);
    reverse.command(0x51, 1); reverse.draw(); reverse.run(ge);
    expect_pixel(memory, backend, 0); // Back-facing normal suppresses specular even at exponent zero.
    DisplayList restore(memory);
    restore.command(0x51, 0); restore.draw(); restore.run(ge);
    expect_pixel(memory, backend, 0xFF0000);

    DisplayList viewer(memory);
    viewer.command(0x91, 0xFFFFFF); viewer.number(0x5B, 2);
    viewer.command(0x3C, 2); viewer.number(0x3D, 1);
    viewer.command(0x3C, 8); viewer.number(0x3D, 0);
    viewer.draw(); viewer.run(ge);
    // N.H = 1/sqrt(2); the GE power/8-bit scale yields 117.
    expect_pixel(memory, backend, 0x757575);

    DisplayList point(memory);
    point.command(0x3C, 2); point.number(0x3D, 0);
    point.command(0x3C, 8); point.number(0x3D, 1);
    point.command(0x55, 0xFFFFFF); point.command(0x5F, 0x100);
    point.command(0x8F, 0xFFFFFF); point.number(0x7B, 2);
    point.draw(); point.run(ge);
    expect_pixel(memory, backend, 0x808080);
    DisplayList zero_denominator(memory);
    zero_denominator.number(0x7B, 0); zero_denominator.draw(); zero_denominator.run(ge);
    expect_pixel(memory, backend, 0);
    DisplayList spot_outside(memory);
    spot_outside.command(0x5F, 0x200); spot_outside.number(0x7B, 2);
    spot_outside.number(0x71, 1); spot_outside.number(0x8B, .9f);
    spot_outside.draw(); spot_outside.run(ge);
    expect_pixel(memory, backend, 0);
    DisplayList spot_inside(memory);
    spot_inside.number(0x8B, .5f); spot_inside.number(0x87, 0);
    spot_inside.draw(); spot_inside.run(ge);
    expect_pixel(memory, backend, 0x808080);

    // Secondary color has no alpha. The same global/material ambient alpha
    // product remains 64 even when a full-bright highlight is added.
    DisplayList alpha(memory);
    alpha.command(0x5F, 1); alpha.command(0x8F, 0); alpha.number(0x5B, 2);
    alpha.command(0x58, 128); alpha.command(0x5D, 128); alpha.command(0x5E, 1);
    alpha.command(0x21, 1); alpha.command(0xDF, 0xA2); alpha.command(0xE1, 0);
    alpha.draw(); alpha.run(ge);
    expect_pixel(memory, backend, 0x404040); // full highlight * source alpha 64/255

    // Distinct clip w values must not distort color interpolation: the PSP
    // interpolates primary and secondary RGB in screen space. Only UV uses w.
    const std::array<float, 18> perspective_data = {0, 0, 1, -1.5f, -1.5f, 0,
                                                  1, 0, 0, 3, -3, 2,
                                                  1, 0, 0, 0, 6, 6};
    for (unsigned i = 0; i < perspective_data.size(); ++i)
        memory.store32(vertices + i * 4u, std::bit_cast<std::uint32_t>(perspective_data[i]));
    const std::array<float, 16> projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2};
    DisplayList interpolate(memory);
    interpolate.command(0x21, 0);
    interpolate.command(0x50, 1); interpolate.command(0x91, 0x0000FF);
    interpolate.command(0x3E, 0);
    for (float value : projection) interpolate.number(0x3F, value);
    interpolate.draw(); interpolate.run(ge);
    // Pixel center (32.5,32.5) has barycentric weight 0.234375 for red A.
    expect_pixel(memory, backend, 60, 1); // software truncates; GL rounds to RGBA8.
    DisplayList primary_interpolate(memory);
    primary_interpolate.command(0x5E, 0); primary_interpolate.draw(); primary_interpolate.run(ge);
    expect_pixel(memory, backend, 60, 1);

    // Flat shading must retain the original last vertex's color when that
    // vertex is clipped away, rather than choosing a generated intersection.
    const std::array<float, 18> clip_data = {1, 0, 0, -.8f, -.8f, 0,
                                           1, 0, 0, .8f, -.8f, 0,
                                           0, 0, 1, 0, .8f, -1.5f};
    for (unsigned i = 0; i < clip_data.size(); ++i)
        memory.store32(vertices + i * 4u, std::bit_cast<std::uint32_t>(clip_data[i]));
    DisplayList clip(memory);
    clip.command(0x50, 0); clip.command(0x3E, 0);
    for (unsigned i = 0; i < 16; ++i) clip.number(0x3F, i % 5 == 0 ? 1.0f : 0.0f);
    clip.draw(); clip.run(ge);
    expect_pixel(memory, backend, 0x0000FF);
    DisplayList separate_clip(memory);
    separate_clip.command(0x5E, 1); separate_clip.command(0x1E, 1);
    separate_clip.draw(); separate_clip.run(ge);
    expect_pixel(memory, backend, 0x0000FF);

    // With a color attribute present, material bit 4 tracks specular RGB.
    const std::array<float, 18> colored_data = {0, 0, 1, -.8f, -.8f, 0,
                                              0, 0, 1, .8f, -.8f, 0,
                                              0, 0, 1, 0, .8f, 0};
    for (unsigned v = 0; v < 3; ++v) {
        memory.store32(vertices + v * 28, 0xFF00FF00);
        for (unsigned i = 0; i < 6; ++i)
            memory.store32(vertices + v * 28 + 4 + i * 4, std::bit_cast<std::uint32_t>(colored_data[v * 6 + i]));
    }
    DisplayList tracked_specular(memory);
    tracked_specular.command(0x12, (7u << 2u) | (3u << 5u) | (3u << 7u));
    tracked_specular.command(0x53, 4); tracked_specular.command(0x91, 0xFFFFFF);
    tracked_specular.draw(); tracked_specular.run(ge);
    expect_pixel(memory, backend, 0x00FF00);
    DisplayList untracked_specular(memory);
    untracked_specular.command(0x53, 0); untracked_specular.draw(); untracked_specular.run(ge);
    expect_pixel(memory, backend, 0xFFFFFF);
}

void texture_coordinates(bool use_gl) {
    std::cout << "Texture-coordinate tests: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    auto *backend = use_gl ? &gl : nullptr;
    constexpr std::uint32_t texture = vertices + 0x1000;
    // Each nearest-filtered texel identifies its x/y with separate channels.
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 16; ++x)
            memory.store32(texture + (y * 16 + x) * 4, 0xFF400000 | (y * 16 << 8) | (x * 16));
    auto geometry = [&](const std::array<float, 24> &data) {
        // UV float2, normal float3, position float3.
        for (unsigned i = 0; i < data.size(); ++i)
            memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(data[i]));
    };
    const std::array<float, 24> normal_data = {0, 0, 0, 0, 2, -.75f, -.75f, 0,
                                             0, 0, 0, 0, 2, .75f, -.75f, 0,
                                             0, 0, 0, 0, 2, 0, .75f, 0};
    geometry(normal_data);
    auto matrix = [&](DisplayList &commands, const std::array<float, 12> &values) {
        commands.command(0x40, 0);
        for (float value : values) commands.number(0x41, value);
    };
    auto identity_projection = [&](DisplayList &commands) {
        commands.command(0x3E, 0);
        for (unsigned i = 0; i < 16; ++i) commands.number(0x3F, i % 5 == 0 ? 1.0f : 0.0f);
    };
    DisplayList setup(memory);
    setup.command(0x9C, 0); setup.command(0x9D, 64); setup.command(0xD2, 3);
    setup.command(0xD4, 0); setup.command(0xD5, 63 | (63 << 10));
    setup.command(0x12, 3u | (3u << 5u) | (3u << 7u));
    setup.number(0x42, 32); setup.number(0x43, 32); setup.number(0x44, 32767);
    setup.number(0x45, 32); setup.number(0x46, 32); setup.number(0x47, 32767);
    setup.command(0x55, 0xFFFFFF); setup.command(0x58, 255);
    setup.command(0xA0, texture & 0xFFFFFF); setup.command(0xA8, 0x080010);
    setup.command(0xB8, 0x0404); setup.command(0xC3, 3); setup.command(0x1E, 1);
    setup.command(0xC7, 0x0101); setup.command(0xC9, 3); // clamp, replace RGB
    setup.number(0x48, 7); setup.number(0x49, 5); // ignored by matrix/shade modes
    setup.number(0x4A, 3); setup.number(0x4B, 2);
    setup.command(0xC0, 0x0201); // normalized model normal
    matrix(setup, {0, 0, 0, 0, 0, 0, .25f, 0, 0, .125f, .25f, 1});
    setup.draw(); setup.run(ge);
    expect_pixel(memory, backend, 0x404060); // (6,4), not raw normal's (10,4)
    DisplayList raw_normal(memory);
    raw_normal.command(0xC0, 0x0301); raw_normal.draw(); raw_normal.run(ge);
    expect_pixel(memory, backend, 0x4040A0);
    DisplayList reversed(memory);
    reversed.command(0x51, 1); reversed.command(0xC0, 0x0201);
    reversed.draw(); reversed.run(ge);
    expect_pixel(memory, backend, 0x404000); // negative s clamps at zero

    // Different texture q AND clip w: divide interpolated s/w by q/w,
    // rather than projecting each vertex's s/q before interpolation.
    geometry({0, 0, 0, 0, 1, -.75f, -.75f, -1,
              0, 0, 0, 0, 1, 1.5f, -1.5f, 0,
              0, 0, 0, 0, 1, 0, 3, 2});
    DisplayList projective(memory);
    projective.command(0x51, 0); projective.command(0xC0, 1);
    matrix(projective, {0, 0, 0, 0, 0, 0, 0, 0, 1, .5f, .25f, 2});
    projective.command(0x3E, 0);
    for (float value : std::array<float, 16>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2})
        projective.number(0x3F, value);
    projective.draw(); projective.run(ge);
    // At (32.5,32.5): screen weights 45/192,49/192,98/192;
    // s/q = .24479166, t/q = .12239583, hence nearest texel (3,1).
    expect_pixel(memory, backend, 0x401030);

    // PSP's documented index formats are 8/16-bit. Leave holes and use high
    // 16-bit indices so treating this as a plain draw or byte indices fails.
    for (unsigned index_type : {1u, 2u}) {
        for (bool shared : {false, true}) {
            constexpr auto indices = vertices + 0x800;
            geometry({}); // a plain draw is degenerate
            const unsigned first = index_type == 1 ? 2 : 258;
            const std::array<float, 24> triangle = {0, 0, 0, 0, 1, -.75f, -.75f, -1,
                                                   0, 0, 0, 0, 1, 1.5f, -1.5f, 0,
                                                   0, 0, 0, 0, 1, 0, 3, 2};
            for (unsigned v = 0; v < 3; ++v)
                for (unsigned c = 0; c < 8; ++c)
                    memory.store32(vertices + ((first + v * 2) * 8 + c) * 4,
                                   std::bit_cast<std::uint32_t>(triangle[v * 8 + c]));
            const unsigned count = shared ? 6 : 3;
            for (unsigned i = 0; i < count; ++i) {
                const auto index = first + (i % 3) * 2;
                if (index_type == 1) memory.store8(indices + i, index);
                else memory.store16(indices + i * 2, index);
            }
            DisplayList indexed(memory);
            indexed.command(0x12, 3u | (3u << 5u) | (3u << 7u) | (index_type << 11u));
            indexed.command(0x10, 0x080000u);
            indexed.command(0x02, indices & 0xFFFFFFu);
            matrix(indexed, {0, 0, 0, 0, 0, 0, 0, 0, 1, shared ? .25f : .75f, .25f, 2});
            indexed.draw(3, count); indexed.run(ge);
            expect_pixel(memory, backend, shared ? 0x401010 : 0x401050);
        }
    }

    // A near-plane intersection must interpolate texture q as well as s/t.
    geometry({0, 0, 0, 0, 1, -.75f, -.75f, 0,
              1, 0, 0, 0, 1, .75f, -.75f, 0,
              3, 0, 0, 0, 1, 0, .75f, -1.5f});
    DisplayList clipped(memory);
    clipped.command(0x12, 3u | (3u << 5u) | (3u << 7u));
    identity_projection(clipped); clipped.command(0xC0, 0x0101);
    matrix(clipped, {0, 0, 1, 0, 0, 0, 0, 0, 0, .5f, .25f, 1});
    clipped.draw(); clipped.run(ge);
    expect_pixel(memory, backend, 0x401020); // interpolated q = 535/192

    // Zero and negative q saturate both axes even if the texture wraps.
    for (float q : {0.0f, -1.0f}) {
        DisplayList nonpositive(memory);
        nonpositive.command(0xC7, 0);
        matrix(nonpositive, {0, 0, 0, 0, 0, 0, 0, 0, 0, -.5f, 2, q});
        nonpositive.draw(); nonpositive.run(ge);
        expect_pixel(memory, backend, 0x40F0F0);
    }
    // Rectangles carry left/right q instead of projecting their endpoints.
    geometry({0, 0, 0, 0, 1, -.75f, -.75f, 0,
              3, 0, 0, 0, 1, .75f, .75f, 0,
              0, 0, 0, 0, 1, 0, 0, 0});
    DisplayList rectangle(memory);
    rectangle.command(0xC7, 0x0101);
    matrix(rectangle, {0, 0, 1, 0, 0, 0, 0, 0, 0, .5f, .25f, 1});
    rectangle.draw(6, 2); rectangle.run(ge);
    expect_pixel(memory, backend, 0x401030);

    // A point's single texture q also divides its generated coordinates.
    geometry({0, 0, 0, 0, 1, 0, 0, 0,
              0, 0, 0, 0, 1, 0, 0, 0,
              0, 0, 0, 0, 1, 0, 0, 0});
    DisplayList point(memory);
    matrix(point, {0, 0, 0, 0, 0, 0, 0, 0, 0, .5f, .25f, 2});
    point.draw(0, 1); point.run(ge);
    expect_pixel(memory, backend, 0x402040);

    // Through vertices bypass texture generation, projection and UV scale.
    geometry({2, 4, 0, 0, 1, 8, 8, 0,
              2, 4, 0, 0, 1, 56, 56, 0,
              0, 0, 0, 0, 1, 0, 0, 0});
    DisplayList through(memory);
    through.command(0x12, 3u | (3u << 5u) | (3u << 7u) | (1u << 23u));
    through.draw(6, 2); through.run(ge);
    expect_pixel(memory, backend, 0x404020);

    // Shade mapping uses the selected light vectors even when lighting and
    // all light channels are disabled. The two selector fields are independent.
    geometry(normal_data);
    DisplayList shade(memory);
    shade.command(0x12, 3u | (3u << 5u) | (3u << 7u));
    shade.command(0xC0, 2); shade.command(0xC1, 1u << 8u);
    shade.number(0x65, 1); shade.number(0x68, -1);
    shade.draw(); shade.run(ge);
    expect_pixel(memory, backend, 0x4000F0);
    DisplayList swap(memory);
    swap.command(0xC1, 1); swap.draw(); swap.run(ge);
    expect_pixel(memory, backend, 0x40F000);
    DisplayList changed_light(memory);
    changed_light.number(0x68, 1); changed_light.draw(); changed_light.run(ge);
    expect_pixel(memory, backend, 0x40F0F0);
    DisplayList reversed_shade(memory);
    reversed_shade.command(0x51, 1); reversed_shade.draw(); reversed_shade.run(ge);
    expect_pixel(memory, backend, 0x400000);

    // A specular shade vector is L + viewer direction, normalized. Only a
    // light-type or view-matrix change must refresh the cached mapping state.
    DisplayList diffuse_shade(memory);
    diffuse_shade.command(0x51, 0);
    diffuse_shade.number(0x66, 1); diffuse_shade.number(0x68, 0);
    diffuse_shade.draw(); diffuse_shade.run(ge);
    expect_pixel(memory, backend, 0x40F080);
    DisplayList specular_shade(memory);
    specular_shade.command(0x60, 1); specular_shade.draw(); specular_shade.run(ge);
    expect_pixel(memory, backend, 0x40F0D0);
    DisplayList view_shade(memory);
    view_shade.command(0x3C, 0);
    for (float value : std::array<float, 12>{1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0})
        view_shade.number(0x3D, value);
    view_shade.draw(); view_shade.run(ge);
    expect_pixel(memory, backend, 0x40F080);

    DisplayList point_shade(memory);
    point_shade.command(0x3C, 0);
    for (float value : std::array<float, 12>{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0})
        point_shade.number(0x3D, value);
    point_shade.command(0x60, 0x0100);
    point_shade.number(0x66, 0); point_shade.number(0x68, 1);
    point_shade.draw(); point_shade.run(ge);
    expect_pixel(memory, backend, 0x40F0D0);
    DisplayList world_shade(memory);
    world_shade.command(0x3A, 0);
    for (float value : std::array<float, 12>{1, 0, 0, 0, 1, 0, 0, 0, 4, 0, 0, .5f})
        world_shade.number(0x3B, value);
    world_shade.draw(); world_shade.run(ge);
    expect_pixel(memory, backend, 0x40F0B0);

    // Mode 3 aliases ordinary UV generation. Explicit zero scales are valid,
    // and switching away from projection must reset the reused vertices' q.
    geometry({.125f, .25f, 0, 0, 1, -.75f, -.75f, 0,
              .125f, .25f, 0, 0, 1, .75f, -.75f, 0,
              .125f, .25f, 0, 0, 1, 0, .75f, 0});
    DisplayList direct(memory);
    direct.command(0xC0, 3);
    direct.number(0x48, 1); direct.number(0x49, 1);
    direct.number(0x4A, 0); direct.number(0x4B, 0);
    direct.draw(); direct.run(ge);
    expect_pixel(memory, backend, 0x404020);
    DisplayList zero_scale(memory);
    zero_scale.number(0x48, 0); zero_scale.number(0x49, 0);
    zero_scale.draw(); zero_scale.run(ge);
    expect_pixel(memory, backend, 0x400000);

    // Fixed-point normals use signed /128 and /32768, including the minimum
    // value -1 exactly. Check both direct decode and the morph accumulation.
    for (unsigned normal_type : {1u, 2u}) {
        for (unsigned morphs : {1u, 2u}) {
            const unsigned position_offset = normal_type == 1 ? 12 : 16;
            const unsigned stride = position_offset + 12;
            const std::array<float, 9> positions = {-.75f, -.75f, 0, .75f, -.75f, 0, 0, .75f, 0};
            for (unsigned vertex = 0; vertex < 3; ++vertex) {
                for (unsigned morph = 0; morph < morphs; ++morph) {
                    const auto address = vertices + (vertex * morphs + morph) * stride;
                    memory.store32(address, 0); memory.store32(address + 4, 0);
                    if (normal_type == 1) {
                        memory.store8(address + 8, 0); memory.store8(address + 9, 0);
                        memory.store8(address + 10, 0x80);
                    } else {
                        memory.store16(address + 8, 0); memory.store16(address + 10, 0);
                        memory.store16(address + 12, 0x8000);
                    }
                    for (unsigned i = 0; i < 3; ++i)
                        memory.store32(address + position_offset + i * 4, std::bit_cast<std::uint32_t>(positions[vertex * 3 + i]));
                }
            }
            DisplayList packed(memory);
            packed.command(0x3A, 0);
            for (float value : std::array<float, 12>{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0})
                packed.number(0x3B, value);
            packed.command(0x12, 3u | (normal_type << 5u) | (3u << 7u) | ((morphs - 1) << 18u));
            packed.number(0x2C, .5f); packed.number(0x2D, .5f);
            packed.command(0xC0, 0x0301);
            matrix(packed, {0, 0, 0, 0, 0, 0, .25f, 0, 0, .5f, .25f, 1});
            packed.draw(); packed.run(ge);
            expect_pixel(memory, backend, 0x404040);
        }
    }
}

void fog_depth(bool use_gl) {
    std::cout << "Fog tests: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    auto *backend = use_gl ? &gl : nullptr;
    const std::array<float, 9> positions = {-.75f, -.75f, -.5f, .75f, -.75f, -.5f, 0, .75f, -.5f};
    for (unsigned i = 0; i < positions.size(); ++i)
        memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(positions[i]));
    DisplayList setup(memory);
    setup.command(0x9C, 0); setup.command(0x9D, 64); setup.command(0xD2, 3);
    setup.command(0xD4, 0); setup.command(0xD5, 63 | (63 << 10));
    setup.command(0x12, 3u << 7u);
    setup.number(0x42, 32); setup.number(0x43, 32); setup.number(0x44, 32767);
    setup.number(0x45, 32); setup.number(0x46, 32); setup.number(0x47, 32767);
    setup.command(0x55, 0x0000FF); setup.command(0x58, 255);
    setup.command(0x1F, 1); setup.number(0xCD, 1); setup.number(0xCE, 1);
    setup.command(0xCF, 0xFF0000);
    setup.draw(); setup.run(ge);
    // Orthographic eye z=-.5, end=1, slope=1 => fog factor 128.
    // RGB rounds up after (source * 128 + fog_color * 127) / 256.
    expect_pixel(memory, backend, 0x7F0080);

    auto geometry = [&](const std::array<float, 9> &data) {
        for (unsigned i = 0; i < data.size(); ++i)
            memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(data[i]));
    };
    auto matrix43 = [&](DisplayList &commands, unsigned number, const std::array<float, 12> &data) {
        commands.command(number, 0);
        for (float value : data) commands.number(number + 1, value);
    };
    auto projection = [&](DisplayList &commands, const std::array<float, 16> &data) {
        commands.command(0x3E, 0);
        for (float value : data) commands.number(0x3F, value);
    };
    const std::array<float, 12> identity43 = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const std::array<float, 16> identity44 = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    DisplayList fog_color(memory);
    fog_color.command(0xCF, 0x00FF00); fog_color.draw(); fog_color.run(ge);
    expect_pixel(memory, backend, 0x007F80);
    DisplayList slope(memory);
    slope.number(0xCE, .5f); slope.draw(); slope.run(ge);
    expect_pixel(memory, backend, 0x00BF40);
    DisplayList end(memory);
    end.number(0xCD, .5f); end.draw(); end.run(ge);
    expect_pixel(memory, backend, 0x00FF00);
    DisplayList saturated(memory);
    saturated.number(0xCD, 3); saturated.draw(); saturated.run(ge);
    expect_pixel(memory, backend, 0x0000FF);
    DisplayList quantized(memory);
    quantized.number(0xCD, .5f + 127.75f / 256); quantized.number(0xCE, 1);
    quantized.draw(); quantized.run(ge);
    expect_pixel(memory, backend, 0x00807F); // factor truncates to 127, not 128

    // Different clip w must not perspective-correct fog. Vertex factors are
    // 0,64,192; screen weights at the sample are 45,49,98 divided by 192.
    geometry({-.75f, -.75f, -1, 1.5f, -1.5f, 0, 0, 3, 2});
    DisplayList affine(memory);
    affine.number(0xCD, 1); affine.number(0xCE, .25f); affine.command(0xCF, 0xFF0000);
    projection(affine, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2});
    affine.draw(); affine.run(ge);
    expect_pixel(memory, backend, 0x8D0072); // flat shading does not flatten fog
    DisplayList gouraud(memory);
    gouraud.command(0x50, 1); gouraud.command(0xCF, 0x00FF00);
    gouraud.draw(); gouraud.run(ge);
    expect_pixel(memory, backend, 0x008D72);

    geometry(positions);
    DisplayList view(memory);
    projection(view, identity44);
    view.command(0xCF, 0xFF0000); view.number(0xCE, 1);
    matrix43(view, 0x3C, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, .25f});
    view.draw(); view.run(ge);
    expect_pixel(memory, backend, 0x3F00C0);
    DisplayList world(memory);
    matrix43(world, 0x3A, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, -.25f});
    world.draw(); world.run(ge);
    expect_pixel(memory, backend, 0x7F0080);

    // Skinning precedes the fog transform. One bone moves z from -.5 to -.25.
    for (unsigned v = 0; v < 3; ++v) {
        memory.store32(vertices + v * 16, std::bit_cast<std::uint32_t>(1.0f));
        for (unsigned c = 0; c < 3; ++c)
            memory.store32(vertices + v * 16 + 4 + c * 4, std::bit_cast<std::uint32_t>(positions[v * 3 + c]));
    }
    DisplayList skinned(memory);
    matrix43(skinned, 0x3A, identity43); matrix43(skinned, 0x3C, identity43);
    matrix43(skinned, 0x2A, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, .25f});
    skinned.command(0x12, (3u << 7u) | (3u << 9u));
    skinned.draw(); skinned.run(ge);
    expect_pixel(memory, backend, 0x3F00C0);

    for (unsigned v = 0; v < 3; ++v)
        for (unsigned m = 0; m < 2; ++m)
            for (unsigned c = 0; c < 3; ++c) {
                const float value = c == 2 ? (m == 0 ? -.25f : -.75f) : positions[v * 3 + c];
                memory.store32(vertices + ((v * 2 + m) * 3 + c) * 4, std::bit_cast<std::uint32_t>(value));
            }
    DisplayList morph(memory);
    morph.command(0x12, (3u << 7u) | (1u << 18u));
    morph.number(0x2C, .5f); morph.number(0x2D, .5f);
    morph.draw(); morph.run(ge);
    expect_pixel(memory, backend, 0x7F0080);

    geometry({-.75f, -.75f, 0, .75f, -.75f, 0, 0, .75f, -1.5f});
    DisplayList clipped(memory);
    clipped.command(0x12, 3u << 7u); clipped.number(0xCE, .5f);
    clipped.draw(); clipped.run(ge);
    expect_pixel(memory, backend, 0xC1003E);

    // Rectangles select the opposite endpoint's fog in each horizontal half.
    geometry({-.75f, -.75f, -.75f, .75f, .75f, -.25f, 0, 0, 0});
    DisplayList rectangle(memory);
    rectangle.number(0xCE, 1); rectangle.draw(6, 2); rectangle.run(ge);
    expect_pixel(memory, backend, 0xBF0040);
    expect_pixel(memory, backend, 0x3F00C0, 0, sample - 12 * 4);
    geometry({.75f, -.75f, -.75f, -.75f, .75f, -.25f, 0, 0, 0});
    DisplayList reversed_rectangle(memory);
    reversed_rectangle.draw(6, 2); reversed_rectangle.run(ge);
    expect_pixel(memory, backend, 0x3F00C0);
    expect_pixel(memory, backend, 0xBF0040, 0, sample - 12 * 4);

    geometry({0, 0, -.5f, 0, 0, 0, 0, 0, 0});
    DisplayList point(memory);
    point.draw(0, 1); point.run(ge);
    expect_pixel(memory, backend, 0x7F0080);

    geometry({8, 8, -.75f, 56, 56, -.25f, 0, 0, 0});
    DisplayList through(memory);
    through.command(0x12, (3u << 7u) | (1u << 23u));
    through.draw(6, 2); through.run(ge);
    expect_pixel(memory, backend, 0x0000FF);
    geometry(positions);
    DisplayList transformed(memory);
    transformed.command(0x12, 3u << 7u); transformed.draw(); transformed.run(ge);
    expect_pixel(memory, backend, 0x7F0080);
    DisplayList clear(memory);
    clear.command(0xD3, 0x101); clear.draw(); clear.run(ge);
    expect_pixel(memory, backend, 0x0000FF);
    DisplayList uncleared(memory);
    uncleared.command(0xD3, 0); uncleared.draw(); uncleared.run(ge);
    expect_pixel(memory, backend, 0x7F0080);
    DisplayList disabled(memory);
    disabled.command(0x1F, 0); disabled.draw(); disabled.run(ge);
    expect_pixel(memory, backend, 0x0000FF);

    // Fog changes RGB only: an alpha test still sees the source alpha 128,
    // including a fully fogged pixel. Rejecting it would leave the red pixel.
    DisplayList alpha(memory);
    alpha.command(0x1F, 1); alpha.number(0xCD, .5f);
    alpha.command(0x58, 128); alpha.command(0x22, 1); alpha.command(0xDB, 0xFF8002);
    alpha.draw(); alpha.run(ge);
    expect_pixel(memory, backend, 0xFF0000);

    // Positive/negative nonfinite final factors follow their sign.
    for (std::uint32_t bits : {0x7F800000u, 0xFF800000u, 0x7FC00000u, 0xFFC00000u}) {
        DisplayList nonfinite(memory);
        nonfinite.number(0xCD, std::bit_cast<float>(bits));
        nonfinite.draw(); nonfinite.run(ge);
        expect_pixel(memory, backend, bits & 0x80000000u ? 0xFF0000 : 0x0000FF);
    }
}

// Destination-only modulation uses PSP's doubled source-alpha factors.
// A fixed zero source must still allow destination brightening above 1x.
void doubled_destination_alpha(bool use_gl) {
    std::cout << "Doubled alpha tests: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    auto *backend = use_gl ? &gl : nullptr;
    const std::array<float, 9> data = {8, 8, 0, 56, 8, 0, 32, 56, 0};
    for (unsigned i=0; i<data.size(); ++i)
        memory.store32(vertices + i*4u, std::bit_cast<std::uint32_t>(data[i]));
    DisplayList setup(memory);
    setup.command(0x9C, 0); setup.command(0x9D, 64); setup.command(0xD2, 3);
    setup.command(0xD4, 0); setup.command(0xD5, 63 | (63 << 10));
    setup.command(0x12, (3u << 7u) | (1u << 23u)); setup.run(ge);
    for (unsigned equation : {0u, 2u}) for (unsigned factor : {6u, 7u})
    for (unsigned alpha : {0u, 64u, 128u, 192u, 255u}) {
        DisplayList background(memory);
        background.command(0x21, 0); background.command(0x55, 0xC08040); background.command(0x58, 255);
        background.draw(); background.run(ge);
        DisplayList overlay(memory);
        overlay.command(0x21, 1); overlay.command(0xDF, 10u | (factor << 4) | (equation << 8));
        overlay.command(0xE0, 0); overlay.command(0x55, 0xFF00FF); overlay.command(0x58, alpha);
        overlay.draw(); overlay.run(ge);
        unsigned expected=0;
        for (unsigned channel=0; channel<3; ++channel) {
            const float scale=factor==6 ? 2.0f*alpha/255.0f : 1.0f-2.0f*alpha/255.0f;
            const auto value=static_cast<unsigned>(std::clamp((64.0f+64.0f*channel)*scale,0.0f,255.0f));
            expected|=value<<(channel*8);
        }
        expect_pixel(memory,backend,expected,1);
    }
}

void exact_blend_rounding(bool use_gl) {
    std::cout << "Exact blend rounding: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    const std::array<float, 6> data = {0, 0, 0, 64, 64, 0};
    for (unsigned i = 0; i < data.size(); ++i)
        memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(data[i]));
    auto draw = [&](unsigned alpha, unsigned equation, unsigned source_factor, unsigned dest_factor,
                    unsigned expected, unsigned destination_alpha = 0) {
        DisplayList background(memory);
        background.command(0x9C, 0); background.command(0x9D, 64); background.command(0xD2, 3);
        background.command(0xD4, 0); background.command(0xD5, 63 | (63 << 10));
        background.command(0x12, (3u << 7u) | (1u << 23));
        background.command(0x21, 0); background.command(0xD3, 0x301);
        background.command(0x55, 0x489A79); background.command(0x58, destination_alpha);
        background.draw(6, 2); background.run(ge);
        DisplayList overlay(memory);
        overlay.command(0xD3, 0); overlay.command(0x21, 1);
        overlay.command(0xDF, source_factor | (dest_factor << 4) | (equation << 8));
        overlay.command(0xE0, 0xA06020); overlay.command(0xE1, 0x4080C0);
        overlay.command(0x55, 0x103F28); overlay.command(0x58, alpha);
        overlay.draw(6, 2); overlay.run(ge);
        expect_pixel(memory, use_gl ? &gl : nullptr, expected);
    };
    // Captured first-divergent draw: source (40,63,16), alpha 21,
    // destination (121,154,72), separate rounded products -> (114,146,67).
    draw(21, 0, 2, 3, 0x439272);
    const std::array<unsigned, 9> alphas = {0, 1, 21, 64, 127, 128, 192, 254, 255};
    const std::array<unsigned, 9> colors = {0x489A79, 0x489978, 0x439272, 0x3A8264, 0x2C6C50,
                                           0x2C6B50, 0x1D553C, 0x103F28, 0x103F28};
    for (unsigned i = 0; i < alphas.size(); ++i) draw(alphas[i], 0, 2, 3, colors[i]);
    const std::array<unsigned, 5> equations = {0, 0x41886C, 0x103F28, 0x489A79, 0x385B51};
    for (unsigned equation = 1; equation <= 5; ++equation) draw(21, equation, 2, 3, equations[equation - 1]);
    draw(21, 6, 2, 3, 0x103F28); draw(21, 7, 2, 3, 0x103F28); // Software-reference reserved behavior.
    const std::array<unsigned, 11> factors = {0x084C26, 0x4E8D7B, 0x07110D, 0x51C794, 0x44AA7E,
                                             0x122E22, 0x0E231A, 0x49B486, 0x8AFFFD, 0, 0x1C6460};
    for (unsigned factor = 0; factor < factors.size(); ++factor) draw(21, 0, factor, factor, factors[factor], 200);
    draw(21, 0, 15, 15, factors.back(), 200); // Reserved factors use the fixed colors.
}

void ordered_blend_gpu() {
    std::cout << "Ordered GPU blend/depth/stencil/packing tests" << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    assert(gl.init()); ge.set_gl(&gl);
    auto geometry = [&](float x0, float x1, float z = 0) {
        const std::array<float, 6> data = {x0, 0, z, x1, 64, z};
        for (unsigned i = 0; i < data.size(); ++i)
            memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(data[i]));
    };
    auto background = [&](unsigned rgb = 0, unsigned alpha = 64, unsigned format = 3, bool depth = false) {
        DisplayList dl(memory);
        dl.command(0x9C, 0); dl.command(0x9D, 64); dl.command(0xD2, format);
        dl.command(0x9E, 0x100000); dl.command(0x9F, 64);
        dl.command(0xD4, 0); dl.command(0xD5, 63 | (63 << 10));
        dl.command(0x12, (3u << 7u) | (1u << 23));
        dl.command(0x20, 0); dl.command(0x21, 0); dl.command(0x23, 0); dl.command(0x24, 0);
        dl.command(0xD3, depth ? 0x701 : 0x301); dl.command(0x55, rgb); dl.command(0x58, alpha);
        dl.draw(6, 2); dl.run(ge); gl.flush();
    };
    auto overlay = [&](unsigned repeats, unsigned rgb = 0xFFFFFF, unsigned alpha = 128) {
        DisplayList dl(memory);
        dl.command(0xD3, 0); dl.command(0x21, 1); dl.command(0xDF, 2 | (3 << 4));
        dl.command(0x55, rgb); dl.command(0x58, alpha);
        for (unsigned i = 0; i < repeats; ++i) dl.draw(6, 2);
        dl.run(ge);
    };
    for (int scale : {1, 2}) {
        assert(gl.set_render_scale(scale) == scale);
        for (unsigned repeats : {1u, 2u, 3u}) {
            geometry(0, 64); background();
            const auto copies = gl.passes().blend_copies, draws = gl.draw_calls(), downloads = gl.downloads();
            overlay(repeats); gl.flush();
            assert(gl.downloads() == downloads);
            assert(gl.passes().blend_copies - copies == repeats);
            assert(gl.draw_calls() - draws == repeats); // Shared quad edges need no extra copy.
            const unsigned expected = repeats == 1 ? 0x808080 : repeats == 2 ? 0xBFBFBF : 0xDFDFDF;
            expect_pixel(memory, &gl, expected);
            assert(memory.load32(sample) >> 24 == 64); // Source alpha never overwrites stencil.
        }
        geometry(0, 64); background();
        const auto copies = gl.passes().blend_copies, draws = gl.draw_calls();
        geometry(0, 16); overlay(1);
        geometry(32, 48); overlay(1); gl.flush();
        assert(gl.passes().blend_copies - copies == 1);
        assert(gl.draw_calls() - draws == 1); // Disjoint quads stay batched.
        expect_pixel(memory, &gl, 0x808080, 0, 0x04000000 + (32 * 64 + 8) * 4);
        expect_pixel(memory, &gl, 0x808080, 0, 0x04000000 + (32 * 64 + 40) * 4);

        geometry(0, 64); background();
        const auto clipped_copies = gl.passes().blend_copies, clipped_draws = gl.draw_calls();
        geometry(100, 164); overlay(1); gl.flush();
        assert(gl.passes().blend_copies == clipped_copies && gl.draw_calls() == clipped_draws);
        expect_pixel(memory, &gl, 0); // Entirely outside the scissor: no GPU work.

        // A relocated framebuffer must snapshot at the actual surface origin.
        geometry(0, 64); background(); geometry(0, 32);
        DisplayList alias(memory);
        alias.command(0x9C, 0x210); alias.run(ge); overlay(1);
        expect_pixel(memory, &gl, 0x808080, 0, 0x04000000 + 0x210 + (16 * 64 + 16) * 4);

        // Refresh destination alpha between overlapping groups, after stencil
        // increments. 128*(64/255) -> 32, then 32*(65/255) -> 8.
        geometry(0, 64); background(0x808080, 64);
        DisplayList stencil(memory);
        stencil.command(0xD3, 0); stencil.command(0x21, 1); stencil.command(0xDF, 10 | (4 << 4));
        stencil.command(0xE0, 0); stencil.command(0x55, 0); stencil.command(0x58, 255);
        stencil.command(0x24, 1); stencil.command(0xDC, 0xFF0001); stencil.command(0xDD, 4 << 16);
        stencil.draw(6, 2); stencil.draw(6, 2); stencil.run(ge);
        expect_pixel(memory, &gl, 0x080808);
        assert(memory.load32(sample) >> 24 == 66);

        // Source-only equations do not need a destination copy or splitting,
        // while fixed-function stencil must still observe all three draws.
        geometry(0, 64); background();
        const auto source_copies = gl.passes().blend_copies, source_draws = gl.draw_calls();
        DisplayList source_only(memory);
        source_only.command(0xD3, 0); source_only.command(0x21, 1); source_only.command(0xDF, 10 | (10 << 4));
        source_only.command(0xE0, 0xFFFFFF); source_only.command(0xE1, 0); source_only.command(0x55, 0x123456);
        source_only.command(0x24, 1); source_only.command(0xDC, 0xFF0001); source_only.command(0xDD, 4 << 16);
        source_only.draw(6, 2); source_only.draw(6, 2); source_only.draw(6, 2); source_only.run(ge); gl.flush();
        assert(gl.passes().blend_copies == source_copies && gl.draw_calls() - source_draws == 1);
        expect_pixel(memory, &gl, 0x123456);
        assert(memory.load32(sample) >> 24 == 67);

        // Source-only self-texturing must take a fresh source snapshot after
        // each overlapping quad, despite not needing a blend destination.
        geometry(0, 64); background();
        const std::array<float, 10> uv = {.5f, .5f, 0, 0, 0, .5f, .5f, 64, 64, 0};
        for (unsigned i = 0; i < uv.size(); ++i)
            memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(uv[i]));
        const auto feedback = gl.passes().feedback_copies, downloads = gl.downloads();
        DisplayList self(memory);
        self.command(0x12, 3u | (3u << 7u) | (1u << 23)); self.command(0xD3, 0);
        self.command(0x24, 0); self.command(0x1E, 1); self.command(0x21, 1); self.command(0xDF, 10 | (10 << 4));
        self.command(0xE0, 0xFFFFFF); self.command(0xE1, 0); self.command(0x55, 0x404040);
        self.command(0xA0, 0); self.command(0xA8, 0x040040); self.command(0xB8, 0x606);
        self.command(0xC2, 0); self.command(0xC3, 3); self.command(0xC6, 0); self.command(0xC7, 0x101);
        self.command(0xC9, 4); self.draw(6, 2); self.draw(6, 2); self.run(ge); gl.flush();
        assert(gl.downloads() == downloads && gl.passes().feedback_copies - feedback == 2);
        expect_pixel(memory, &gl, 0x808080);

        DisplayList untexture(memory); untexture.command(0x1E, 0); untexture.run(ge);

        // Rejected depth must leave RGB alone and still perform the z-fail op.
        geometry(0, 64, 100); background(0, 64, 3, true);
        geometry(0, 64, 101);
        DisplayList reject(memory);
        reject.command(0x23, 1); reject.command(0xDE, 4); reject.command(0xE7, 0);
        reject.command(0x24, 1); reject.command(0xDC, 0xFF0901); reject.command(0xDD, 2 << 8);
        reject.run(ge); overlay(1);
        expect_pixel(memory, &gl, 0);
        assert(memory.load32(sample) >> 24 == 9);

        geometry(0, 64); background();
        DisplayList alpha_reject(memory);
        alpha_reject.command(0x22, 1); alpha_reject.command(0xDB, 0xFF0000); // Never.
        alpha_reject.command(0x24, 1); alpha_reject.command(0xDC, 0xFF0001); alpha_reject.command(0xDD, 4 << 16);
        alpha_reject.run(ge); overlay(1);
        expect_pixel(memory, &gl, 0);
        assert(memory.load32(sample) >> 24 == 64); // Discard precedes stencil.
        DisplayList alpha_reset(memory); alpha_reset.command(0x22, 0); alpha_reset.run(ge);

        for (unsigned format = 0; format < 4; ++format) for (unsigned rgb : {0x808080u, 0xFFFFFFu}) {
            geometry(0, 64); background(rgb, 0, format);
            const auto downloads = gl.downloads();
            DisplayList dither(memory);
            dither.command(0xD3, 0); dither.command(0x21, 1); dither.command(0xDF, 10 | (10 << 4));
            dither.command(0xE0, 0xFFFFFF); dither.command(0xE1, 0xFFFFFF);
            dither.command(0x55, 0x080808); dither.command(0x58, 255);
            dither.command(0x20, 1);
            for (unsigned row = 0; row < 4; ++row) dither.command(0xE2 + row, 0x8888); // -8
            dither.draw(6, 2); dither.run(ge); gl.flush();
            assert(gl.downloads() == downloads);
            if (format == 3) expect_pixel(memory, &gl, rgb);
            else {
                const auto address = 0x04000000 + (32 * 64 + 32) * 2;
                gl.prepare_vram_read(address, 2);
                const unsigned gray[] = {0x8410, 0x4210, 0x0888}, white[] = {0xFFFF, 0x7FFF, 0x0FFF};
                assert(memory.load16(address) == (rgb == 0x808080 ? gray[format] : white[format]));
            }
        }
    }
}

void interpolated_color_precision(bool use_gl) {
    std::cout << "Interpolated color precision: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    struct Vertex { unsigned rgba; float x, y, z; };
    constexpr unsigned texture = vertices + 0x1000;
    memory.store32(texture, 0xFFFFFFFF);
    for (bool alpha_gradient : {false, true}) for (bool textured : {false, true}) for (bool blended : {false, true}) {
        const unsigned top = alpha_gradient ? 0x46FFFFFF : 0xFF464646;
        const unsigned bottom = alpha_gradient ? 0x1BFFFFFF : 0xFF1B1B1B;
        const std::array<Vertex, 4> data = {{{top, 0, 0, 0}, {top, 64, 0, 0},
                                           {bottom, 0, 272, 0}, {bottom, 64, 272, 0}}};
        const std::array<float, 6> clear_data = {0, 0, 0, 64, 272, 0};
        for (unsigned i = 0; i < clear_data.size(); ++i)
            memory.store32(vertices + i * 4, std::bit_cast<unsigned>(clear_data[i]));
        DisplayList clear(memory);
        clear.command(0x9C, 0); clear.command(0x9D, 64); clear.command(0xD2, 3);
        clear.command(0xD4, 0); clear.command(0xD5, 63 | (271 << 10));
        clear.command(0x12, (3u << 7u) | (1u << 23)); clear.command(0x1E, 0);
        clear.command(0x21, 0); clear.command(0x22, 0); clear.command(0xD3, 0x301);
        clear.command(0x55, 0); clear.command(0x58, 0); clear.draw(6, 2); clear.run(ge);
        for (unsigned i = 0; i < data.size(); ++i) {
            const unsigned base = vertices + i * (textured ? 24 : 16);
            if (textured) { memory.store32(base, std::bit_cast<unsigned>(.5f)); memory.store32(base + 4, std::bit_cast<unsigned>(.5f)); }
            const unsigned pos = base + (textured ? 8 : 0);
            memory.store32(pos, data[i].rgba);
            memory.store32(pos + 4, std::bit_cast<unsigned>(data[i].x));
            memory.store32(pos + 8, std::bit_cast<unsigned>(data[i].y));
            memory.store32(pos + 12, 0);
        }
        DisplayList dl(memory);
        dl.command(0x9C, 0); dl.command(0x9D, 64); dl.command(0xD2, 3);
        dl.command(0xD4, 0); dl.command(0xD5, 63 | (271 << 10));
        dl.command(0x12, (7 << 2) | (3 << 7) | (1 << 23) | (textured ? 3 : 0));
        dl.command(0x50, 1); dl.command(0x1E, textured ? 1 : 0); dl.command(0x21, blended ? 1 : 0);
        dl.command(0xD3, 0);
        dl.command(0xDF, 2 | (3 << 4));
        dl.command(0xA0, texture & 0xFFFFFF); dl.command(0xA8, 0x080001); dl.command(0xB8, 0);
        dl.command(0xC2, 0); dl.command(0xC3, 3); dl.command(0xC6, 0); dl.command(0xC9, 0x100);
        dl.command(0x22, alpha_gradient ? 1 : 0); dl.command(0xDB, 2 | (46 << 8) | 0xFF0000);
        dl.draw(4, 4); dl.run(ge);
        // Hardware floor: 70 - 43*(145.5/272) = 46.998..., truncated to 46.
        const unsigned expected = alpha_gradient && !blended ? 0xFFFFFF : 0x2E2E2E;
        expect_pixel(memory, use_gl ? &gl : nullptr, expected, 0, 0x04000000 + (145 * 64 + 24) * 4);
    }
    // GE channels truncate: 6.5 -> 6, 7.5 -> 7, 8.5 -> 8.
    // Alpha 7.5 must become 7 before the alpha test as well.
    const std::array<Vertex, 4> ties = {{{0x06070605, 0, 0, 0}, {0x0A0B0A09, 4, 0, 0},
                                       {0x06070605, 0, 64, 0}, {0x0A0B0A09, 4, 64, 0}}};
    for (unsigned i = 0; i < ties.size(); ++i) {
        const auto address = vertices + i * 16;
        memory.store32(address, ties[i].rgba);
        memory.store32(address + 4, std::bit_cast<unsigned>(ties[i].x));
        memory.store32(address + 8, std::bit_cast<unsigned>(ties[i].y)); memory.store32(address + 12, 0);
    }
    DisplayList tied(memory);
    tied.command(0x12, (7 << 2) | (3 << 7) | (1 << 23)); tied.command(0x1E, 0);
    tied.command(0x21, 1); tied.command(0xDF, 10 | (10 << 4));
    tied.command(0xE0, 0xFFFFFF); tied.command(0xE1, 0); tied.command(0x22, 1);
    tied.command(0xDB, 2 | (7 << 8) | 0xFF0000);
    tied.draw(4, 4); tied.run(ge);
    expect_pixel(memory, use_gl ? &gl : nullptr, 0x080706, 0, 0x04000000 + (32 * 64 + 1) * 4);
}

void interpolated_secondary_precision() {
    std::cout << "Interpolated secondary color: GLES3" << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    assert(gl.init());
    pspweb::GlDrawState state{};
    state.fb_stride = 64; state.fb_format = 3; state.sx2 = state.sy2 = 63;
    state.blend = true; state.src_factor = state.dst_factor = 10; state.fix_a = 0xFFFFFF;
    gl.set_state(state);
    auto vertex = [](float x, float y, unsigned r, unsigned g, unsigned b) {
        pspweb::GlVertex v{}; v.x = x; v.y = y; v.w = 1; v.a = 255;
        v.sr = static_cast<std::uint8_t>(r); v.sg = static_cast<std::uint8_t>(g); v.sb = static_cast<std::uint8_t>(b);
        return v;
    };
    const auto a = vertex(0, 0, 5, 6, 7), b = vertex(4, 0, 9, 10, 11);
    const auto c = vertex(0, 64, 5, 6, 7), d = vertex(4, 64, 9, 10, 11);
    gl.triangle(a, b, c); gl.triangle(b, d, c); gl.flush();
    assert(gl.downloads() == 0);
    expect_pixel(memory, &gl, 0x080706, 0, 0x04000000 + (32 * 64 + 1) * 4);
    // Vertex-only changes must toggle the zero-secondary fast path even when
    // every draw-state register is unchanged.
    auto plain = [](pspweb::GlVertex v) {
        v.sr = v.sg = v.sb = 0; v.r = 17; v.g = 18; v.b = 19;
        return v;
    };
    gl.triangle(plain(a), plain(b), plain(c)); gl.triangle(plain(b), plain(d), plain(c));
    expect_pixel(memory, &gl, 0x131211, 0, 0x04000000 + (32 * 64 + 1) * 4);
    gl.triangle(a, b, c); gl.triangle(b, d, c);
    expect_pixel(memory, &gl, 0x080706, 0, 0x04000000 + (32 * 64 + 1) * 4);
}

void texture_function_precision(bool use_gl) {
    std::cout << "Texture-function integer precision: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    constexpr unsigned texture = vertices + 0x1000;
    // The first case distinguishes /256 with a boosted primary from /255.
    struct Case { unsigned primary, texel, function, expected; bool doubled; };
    const Case cases[] = {
        {0x0AC8C8C8, 0x14C8C8C8, 0, 0x9D9D9D, false},
        {0x0A80500A, 0x143C2814, 0, 0x1E0C00, false},
        {0x0A80500A, 0x143C2814, 0, 0x3C1901, true},
        {0x0A80500A, 0x143C2814, 1, 0x7B4D0B, false},
        {0x0A80500A, 0x143C2814, 1, 0xF69B17, true},
        {0x0A80500A, 0x143C2814, 2, 0x9A5B0C, false},
        {0x0A80500A, 0x143C2814, 2, 0xFFB719, true},
        {0x0A80500A, 0x143C2814, 3, 0x3C2814, false},
        {0x0A80500A, 0x143C2814, 3, 0x785028, true},
        {0x0A80500A, 0x143C2814, 4, 0xBC781E, false},
        {0x0A80500A, 0x143C2814, 4, 0xFFF03C, true},
    };
    const std::array<float, 10> data = {.5f, .5f, 0, 0, 0, .5f, .5f, 64, 64, 0};
    for (unsigned i = 0; i < data.size(); ++i)
        memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(data[i]));
    for (bool rgba : {true, false}) for (const auto &test : cases) {
        memory.store32(texture, test.texel);
        ge.next_frame();
        DisplayList background(memory);
        background.command(0x9C, 0); background.command(0x9D, 64); background.command(0xD2, 3);
        background.command(0xD4, 0); background.command(0xD5, 63 | (63 << 10));
        background.command(0x12, 3u | (3u << 7u) | (1u << 23));
        background.command(0x1E, 0); background.command(0x22, 0);
        background.command(0x55, 0xFF00FF); background.draw(6, 2); background.run(ge);
        DisplayList dl(memory);
        dl.command(0x12, 3u | (3u << 7u) | (1u << 23)); dl.command(0x1E, 1);
        dl.command(0x55, test.primary); dl.command(0x58, test.primary >> 24);
        dl.command(0xA0, texture & 0xFFFFFF); dl.command(0xA8, 0x080001);
        dl.command(0xB8, 0); dl.command(0xC2, 0); dl.command(0xC3, 3);
        dl.command(0xC6, 0); dl.command(0xC7, 0x101);
        dl.command(0xC9, test.function | (rgba ? 0x100 : 0) | (test.doubled ? 0x10000 : 0));
        dl.command(0xCA, 0xF0961E);
        // Alpha is tested before stencil takes ownership of framebuffer alpha.
        unsigned alpha = !rgba || test.function == 1 ? 10 : test.function == 3 ? 20 : 0;
        dl.command(0x22, 1); dl.command(0xDB, 2 | (alpha << 8) | 0xFF0000);
        dl.draw(6, 2); dl.run(ge);
        unsigned expected = test.expected;
        if (!rgba && test.function == 1) expected = test.doubled ? 0x785028 : 0x3C2814;
        expect_pixel(memory, use_gl ? &gl : nullptr, expected);
    }
    // Constant byte colors must survive perspective interpolation at every
    // interior pixel, including non-power-of-two channels and varying clip w.
    memory.store32(texture, 0xFFFFFFFF);
    ge.next_frame();
    const std::array<float, 15> perspective = {.5f, .5f, -.75f, -.75f, 1,
                                              .5f, .5f, .75f, -.75f, 1,
                                              .5f, .5f, 0, 3, 4};
    for (unsigned i = 0; i < perspective.size(); ++i)
        memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(perspective[i]));
    DisplayList projected(memory);
    projected.command(0x12, 3u | (3u << 7u));
    projected.command(0x22, 0); projected.command(0xC9, 0x100);
    projected.command(0x55, 0x489A79); projected.command(0x58, 255);
    projected.number(0x42, 32); projected.number(0x43, 32); projected.number(0x44, 32767);
    projected.number(0x45, 32); projected.number(0x46, 32); projected.number(0x47, 32767);
    projected.command(0x3E, 0);
    for (float value : std::array<float, 16>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0})
        projected.number(0x3F, value);
    projected.draw(); projected.run(ge);
    for (unsigned y = 20; y < 36; ++y) for (unsigned x = 24; x < 40; ++x)
        expect_pixel(memory, use_gl ? &gl : nullptr, 0x489A79, 0, 0x04000000 + (y * 64 + x) * 4);
}

void presentation_sampler_state() {
    std::cout << "Presentation sampler-state tests: GLES3" << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    assert(gl.init());
    // Alternating framebuffer pixels must remain sharp at 2x presentation.
    for (unsigned y = 0; y < 272; ++y) for (unsigned x = 0; x < 512; ++x)
        memory.store32(0x04000000 + (y * 512 + x) * 4, x & 1 ? 0xFF00FF00 : 0xFF0000FF);
    GLuint texture = 0, target = 0, sampler = 0;
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 960, 544, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &target); glBindFramebuffer(GL_FRAMEBUFFER, target);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glGenSamplers(1, &sampler);
    bool matches = true;
    for (GLint min_filter : {GL_LINEAR, GL_NEAREST_MIPMAP_NEAREST}) {
        glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, min_filter);
        glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindSampler(0, sampler); // State left by a textured game draw.
        gl.present(0x04000000, 512, 3, target, 960, 544);
        std::array<unsigned char, 4> pixel{};
        glReadPixels(1, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        assert(glGetError() == GL_NO_ERROR);
        if (pixel[0] != 255 || pixel[1] != 0 || pixel[2] != 0)
            std::cerr << "present: expected red, got " << unsigned(pixel[0]) << "," << unsigned(pixel[1]) << "," << unsigned(pixel[2]) << '\n';
        matches &= pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0;
    }
    assert(matches);
    glBindSampler(0, 0); glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteSamplers(1, &sampler); glDeleteFramebuffers(1, &target); glDeleteTextures(1, &texture);
}

void rgb565_destination_alpha(bool use_gl) {
    std::cout << "RGB565 destination-alpha tests: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    const std::array<float, 6> data = {0, 0, 0, 64, 64, 0};
    for (unsigned i = 0; i < data.size(); ++i)
        memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(data[i]));
    for (bool source_factor : {true, false}) for (unsigned factor : {4u, 5u, 8u, 9u}) {
        DisplayList background(memory);
        background.command(0x9C, 0); background.command(0x9D, 64); background.command(0xD2, 0);
        background.command(0xD4, 0); background.command(0xD5, 63 | (63 << 10));
        background.command(0x12, (3u << 7u) | (1u << 23));
        background.command(0x21, 0); background.command(0x55, 0x0000FF); background.command(0x58, 255);
        background.draw(6, 2); background.run(ge);
        DisplayList blend(memory);
        blend.command(0x21, 1); blend.command(0x55, 0x00FF00);
        blend.command(0xDF, source_factor ? factor | (10 << 4) : 10 | (factor << 4));
        blend.command(0xE0, 0); blend.command(0xE1, 0); blend.draw(6, 2); blend.run(ge);
        const unsigned address = 0x04000000 + (32 * 64 + 32) * 2;
        if (use_gl) { gl.flush(); gl.prepare_vram_read(address, 2); }
        const unsigned expected = (factor & 1) ? (source_factor ? 0x07E0 : 0x001F) : 0;
        const auto actual = memory.load16(address);
        if (actual != expected) std::cerr << "565 blend: expected 0x" << std::hex << expected << ", got 0x" << actual << std::dec << '\n';
        assert(actual == expected);
    }
    // The zero destination blend alpha does not make a 565 texture transparent.
    DisplayList green(memory);
    green.command(0x21, 0); green.command(0x55, 0x00FF00); green.draw(6, 2); green.run(ge);
    DisplayList red(memory);
    red.command(0x9C, 0x60000); red.command(0xD2, 3); red.command(0x55, 0x0000FF);
    red.draw(6, 2); red.run(ge);
    const std::array<float, 10> uv = {0, 0, 0, 0, 0, 64, 64, 64, 64, 0};
    for (unsigned i = 0; i < uv.size(); ++i)
        memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(uv[i]));
    DisplayList textured(memory);
    textured.command(0x12, 3u | (3u << 7u) | (1u << 23)); textured.command(0x1E, 1);
    textured.command(0xA0, 0); textured.command(0xA8, 0x040040); textured.command(0xB8, 0x606);
    textured.command(0xC3, 0); textured.command(0xC9, 0x103);
    textured.command(0x22, 1); textured.command(0xDB, 0xFFFF02);
    textured.draw(6, 2); textured.run(ge);
    expect_pixel(memory, use_gl ? &gl : nullptr, 0x00FF00, 0, sample + 0x60000);
}

void ordered_dither(bool use_gl) {
    std::cout << "Ordered dither tests: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    const std::array<std::array<unsigned, 4>, 2> matrices = {{{0x1D0C, 0xF3E2, 0x0C1D, 0xE2F3},
                                                               {0x789F, 0, 0xFFFF, 0x8888}}};
    const std::array<float, 6> data = {0, 0, 0, 64, 64, 0};
    for (unsigned i = 0; i < data.size(); ++i)
        memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(data[i]));
    for (int scale : {1, 2}) {
        if (!use_gl && scale != 1) continue;
        if (use_gl) assert(gl.set_render_scale(scale) == scale);
        for (const auto &rows : matrices)
        for (unsigned format : {0u, 1u, 2u, 3u})
        for (unsigned offset : {0u, 64u * (format == 3 ? 4u : 2u)})
        for (unsigned color : {0x786860u, 0u, 0xFFFFFFu})
        for (bool clear : {false, true})
        for (bool enabled : {true, false}) {
            DisplayList dl(memory);
            dl.command(0x9C, offset); dl.command(0x9D, 64); dl.command(0xD2, format);
            dl.command(0xD4, 0); dl.command(0xD5, 63 | (63 << 10));
            dl.command(0x12, (3u << 7u) | (1u << 23));
            dl.command(0x55, color); dl.command(0x58, 255); dl.command(0xD3, clear ? 0x101 : 0);
            dl.command(0x20, enabled ? 1 : 0);
            for (unsigned i = 0; i < rows.size(); ++i) dl.command(0xE2 + i, rows[i]);
            dl.draw(6, 2); dl.run(ge);
            if (use_gl) { gl.flush(); gl.prepare_vram_read(0x04000000 + offset, 64 * 64 * (format == 3 ? 4 : 2)); }
            for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
                const int nibble = (rows[y] >> (x * 4)) & 15;
                const int delta = enabled ? (nibble ^ 8) - 8 : 0;
                std::array<unsigned, 3> c{};
                for (unsigned k = 0; k < 3; ++k)
                    c[k] = static_cast<unsigned>(std::clamp(static_cast<int>((color >> (k * 8)) & 255) + delta, 0, 255));
                const unsigned address = 0x04000000 + offset + ((32 + y) * 64 + 32 + x) * (format == 3 ? 4 : 2);
                unsigned expected, actual;
                if (format == 3) { expected = c[0] | (c[1] << 8) | (c[2] << 16); actual = memory.load32(address) & 0xFFFFFF; }
                else {
                    expected = format == 0 ? (c[0] >> 3) | ((c[1] >> 2) << 5) | ((c[2] >> 3) << 11)
                             : format == 1 ? (c[0] >> 3) | ((c[1] >> 3) << 5) | ((c[2] >> 3) << 10)
                                           : (c[0] >> 4) | ((c[1] >> 4) << 4) | ((c[2] >> 4) << 8);
                    actual = memory.load16(address) & (format == 0 ? 0xFFFF : format == 1 ? 0x7FFF : 0xFFF);
                }
                if (actual != expected) std::cerr << "dither: expected 0x" << std::hex << expected << ", got 0x" << actual << std::dec << '\n';
                assert(actual == expected);
            }
            if (use_gl) assert(glGetError() == GL_NO_ERROR);
        }
    }
    if (!use_gl) {
        auto blended = [&](unsigned src, unsigned dst, unsigned equation, unsigned expected, unsigned address) {
            DisplayList clear(memory);
            clear.command(0x9C, 0); clear.command(0xD2, 3); clear.command(0x20, 0);
            clear.command(0xD3, 0x101); clear.command(0x55, dst); clear.draw(6, 2); clear.run(ge);
            DisplayList blend(memory);
            blend.command(0xD3, 0); blend.command(0x20, 1); blend.command(0x21, 1);
            blend.command(0x55, src); blend.command(0xDF, (equation << 8) | 0xAA);
            blend.command(0xE0, 0xFFFFFF); blend.command(0xE1, 0xFFFFFF);
            for (unsigned i = 0; i < 4; ++i) blend.command(0xE2 + i, matrices[0][i]);
            blend.draw(6, 2); blend.run(ge);
            expect_pixel(memory, nullptr, expected, 0, address);
        };
        // Dither precedes saturation, rather than adjusting already-clamped RGB.
        blended(0x0A0A0A, 0xFAFAFA, 0, 0xFFFFFF, sample);
        blended(0x040404, 0x0A0A0A, 1, 0, sample + 64 * 4 + 2 * 4);
    }
}

void fixed_uv_planes() {
    std::cout << "Shared UV quantization, rectangle anchors and subtexel taps: GLES3" << std::endl;
    psprecomp::GuestMemory memory; pspweb::GeGl gl(memory); pspweb::Ge ge(memory);
    assert(gl.init()); ge.set_gl(&gl);
    constexpr unsigned texture = vertices + 0x2000;
    auto geometry = [&](const auto &data) {
        gl.flush();
        for (unsigned i = 0; i < data.size(); ++i) memory.store32(vertices + i * 4, std::bit_cast<unsigned>(data[i]));
    };
    auto setup = [&](DisplayList &dl, unsigned stride, unsigned tex_stride, unsigned size, unsigned filter) {
        dl.command(0x9C, 0); dl.command(0x9D, stride); dl.command(0xD2, 3);
        dl.command(0xD4, 0); dl.command(0xD5, (stride - 1) | (63 << 10));
        dl.command(0x12, 3u | (3u << 7) | (1u << 23)); dl.command(0x1E, 1);
        dl.command(0xA0, texture & 0xFFFFFF); dl.command(0xA8, 0x080000 | tex_stride);
        dl.command(0xB8, size); dl.command(0xC2, 0); dl.command(0xC3, 3);
        dl.command(0xC6, filter); dl.command(0xC7, 0); dl.command(0xC8, 1); dl.command(0xC9, 0x103);
    };
    for (unsigned i = 0; i < 4; ++i) memory.store32(texture + i * 4, i == 0 ? 0xFF0000FF : 0xFF00FF00);
    geometry(std::array<float, 15>{4096.9375f, .5f, 8, 8, 0, 4097.1875f, .5f, 56, 8, 0,
                                  4096.9375f, .5f, 32, 56, 0});
    DisplayList shared(memory); setup(shared, 64, 4, 2, 0); shared.draw(); shared.run(ge);
    // Shared 15-bit quantization discards low U bits before interpolation.
    // Ordinary barycentric interpolation crosses into green texel 1 here.
    expect_pixel(memory, &gl, 0x0000FF);

    for (unsigned y = 0; y < 32; ++y) for (unsigned x = 0; x < 512; ++x)
        memory.store32(texture + (y * 512 + x) * 4, y == 9 ? 0xFF0000FF : y == 10 ? 0xFF00FF00 : 0xFFFF0000);
    ge.next_frame();
    geometry(std::array<float, 15>{280, 0, 480, 0, 0, 280, 20, 480, 33, 0, 0, 20, 0, 33, 0});
    DisplayList triangle(memory); setup(triangle, 512, 512, 9 | (5 << 8), 0); triangle.draw(); triangle.run(ge);
    constexpr unsigned anchor_sample = 0x04000000 + (16 * 512 + 240) * 4;
    expect_pixel(memory, &gl, 0x00FF00, 0, anchor_sample);
    geometry(std::array<float, 10>{0, 0, 0, 0, 0, 280, 20, 480, 33, 0});
    DisplayList rectangle(memory); rectangle.draw(6, 2); rectangle.run(ge);
    // Both halves share the rectangle's first virtual-corner plane. An
    // independently anchored second triangle would select green row 10.
    expect_pixel(memory, &gl, 0x0000FF, 0, anchor_sample);

    for (unsigned y = 0; y < 16; ++y) for (unsigned x = 0; x < 64; ++x)
        memory.store32(texture + (y * 64 + x) * 4, x == 63 ? 0xFFFF0000 : 0xFF0000FF);
    ge.next_frame();
    geometry(std::array<float, 10>{0, 0, 0, 0, 0, 49, 15, 49, 15, 0});
    DisplayList filtered(memory); setup(filtered, 64, 64, 6 | (4 << 8), 0x101);
    filtered.draw(6, 2); filtered.run(ge);
    // Non-power-of-two setup puts the first sample just below .5 texel;
    // truncation to 7/16 leaves a 1/16 contribution from wrapped blue.
    expect_pixel(memory, &gl, 0x0F00EF, 0, 0x04000000);
}

void texture_mipmaps() {
    std::cout << "Texture mipmap tests: GLES3" << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    assert(gl.init()); ge.set_gl(&gl);
    constexpr std::uint32_t texture = vertices + 0x1000;
    const std::array<unsigned, 3> colors = {0x0000FF, 0x00FF00, 0xFF0000};
    for (unsigned level = 0; level < 3; ++level) {
        const unsigned size = 64 >> level;
        for (unsigned i = 0; i < size * size; ++i)
            memory.store32(texture + level * 0x4000 + i * 4, colors[level] | 0xFF000000);
    }
    auto draw = [&](unsigned mode, int bias, unsigned filter, float extent, float slope, unsigned expected, float start = 0) {
        const std::array<float, 10> data = {start, .5f, 0, 0, 0, extent + start, .5f, 64, 64, 0};
        for (unsigned i = 0; i < data.size(); ++i)
            memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(data[i]));
        DisplayList dl(memory);
        dl.command(0x9C, 0); dl.command(0x9D, 64); dl.command(0xD2, 3);
        dl.command(0xD4, 0); dl.command(0xD5, 63 | (63 << 10));
        dl.command(0x12, 3u | (3u << 7u) | (1u << 23)); dl.command(0x1E, 1);
        for (unsigned level = 0; level < 3; ++level) {
            dl.command(0xA0 + level, (texture + level * 0x4000) & 0xFFFFFF);
            dl.command(0xA8 + level, 0x080000 | (64 >> level));
            dl.command(0xB8 + level, (6 - level) | ((6 - level) << 8));
        }
        dl.command(0xC2, 2 << 16); dl.command(0xC3, 3); dl.command(0xC9, 3);
        dl.command(0xC6, filter); dl.command(0xC8, mode | ((static_cast<unsigned>(bias) & 255) << 16));
        dl.number(0xD0, slope); dl.draw(6, 2); dl.run(ge);
        expect_pixel(memory, &gl, expected, 1);
    };
    draw(1, 16, 4, 64, 1, colors[1]);
    draw(1, 32, 4, 64, 1, colors[2]);
    draw(1, -16, 4, 64, 1, colors[0]);
    draw(1, 127, 4, 64, 1, colors[2]);
    draw(3, 16, 4, 64, 1, colors[1]); // mode 3 aliases constant
    draw(1, 8, 6, 64, 1, 0x007F7F); // half red / half green
    draw(1, 20, 6, 64, 1, 0x3FBF00); // 3/4 green / 1/4 blue
    draw(1, 8, 4, 64, 1, colors[1]); // nearest mip rounds up at half
    draw(0, 0, 4, 128, 1, colors[1]);
    draw(0, 0, 4, 256, 1, colors[2]);
    draw(0, -16, 4, 256, 1, colors[1]);
    draw(2, 0, 4, 64, 1, colors[1]);
    draw(2, 0, 4, 64, 2, colors[2]);
    draw(2, 0, 4, 64, .5f, colors[0]);
    draw(1, 32, 0, 64, 1, colors[0]); // disabling mipmaps resets the path
    assert(gl.set_render_scale(2) == 2);
    draw(0, 0, 4, 256, 1, colors[2]); // LOD stays in native PSP pixel units
    assert(gl.set_render_scale(1) == 1);
    // A perspective triangle has clip w = 1, 1, 4. At the test pixel,
    // GE uses the interpolated reciprocal plane, rather than screen-linear w.
    // Pinned 61e60ee6ff headless captures of this fixture select mip 0 (red).
    const std::array<float, 15> perspective = {.5f, .5f, -.75f, -.75f, 1,
                                              .5f, .5f, .75f, -.75f, 1,
                                              .5f, .5f, 0, 3, 4};
    for (unsigned i = 0; i < perspective.size(); ++i)
        memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(perspective[i]));
    DisplayList projected(memory);
    projected.command(0x12, 3u | (3u << 7u)); projected.command(0xC6, 4); projected.command(0xC8, 2);
    projected.number(0xD0, .4f);
    projected.number(0x42, 32); projected.number(0x43, 32); projected.number(0x44, 32767);
    projected.number(0x45, 32); projected.number(0x46, 32); projected.number(0x47, 32767);
    projected.command(0x3E, 0);
    for (float value : std::array<float, 16>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0})
        projected.number(0x3F, value);
    projected.draw(); projected.run(ge);
    expect_pixel(memory, &gl, colors[0]);
    // Only the higher RAM level changes, while level zero retains its hash.
    for (unsigned i = 0; i < 32 * 32; ++i) memory.store32(texture + 0x4000 + i * 4, 0xFFFFFFFF);
    ge.next_frame();
    draw(1, 16, 4, 64, 1, 0xFFFFFF);
    for (unsigned level = 0; level < 2; ++level) {
        const unsigned size = 64 >> level;
        for (unsigned y = 0; y < size; ++y) for (unsigned x = 0; x < size; ++x)
            memory.store32(texture + level * 0x4000 + (y * size + x) * 4,
                           x & 1 ? 0xFFFFFFFF : 0xFF000000);
    }
    ge.next_frame();
    // At mip 1, base coordinate 1.25 becomes .625: a 2/16 linear weight.
    draw(1, 16, 4, 0, 1, 0, 1.25f);
    draw(1, 16, 5, 0, 1, 0x1F1F1F, 1.25f);
    draw(1, 16, 0x104, 0, 1, 0, 1.25f);
    // Positive detail selects the min filter even when it rounds to mip zero.
    draw(1, 4, 0x104, 0, 1, 0xFFFFFF, 1.25f);
    draw(1, -16, 0x104, 0, 1, 0xBFBFBF, 1.25f);
}

void mipmap_clut_and_vram() {
    std::cout << "Swizzled indexed mip/cache tests: GLES3" << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    assert(gl.init()); ge.set_gl(&gl);
    constexpr std::uint32_t texture = 0x04100000, palette = vertices + 0x1000;
    const std::array<unsigned, 8> colors = {0x102030, 0x203040, 0x304050, 0x405060,
                                          0x506070, 0x607080, 0x708090, 0x8090A0};
    for (unsigned i = 0; i < 256; ++i) memory.store32(palette + i * 4, 0xFF000000);
    for (unsigned level = 0; level < 8; ++level) {
        memory.store32(palette + level * 16 * 4, colors[level] | 0xFF000000);
        // Fixed pitch 128 texels: each 16-byte by 8-row swizzle tile is backed.
        const unsigned bytes = ((std::max(128u >> level, 1u) + 7) / 8) * 512;
        for (unsigned i = 0; i < bytes; ++i) memory.store8(texture + level * 0x4000 + i, 0);
    }
    memory.store32(palette + 113 * 4, 0xFFFFFFFF);
    const std::array<float, 10> data = {0, 0, 0, 0, 0, 128, 128, 64, 64, 0};
    for (unsigned i = 0; i < data.size(); ++i)
        memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(data[i]));
    auto draw = [&](unsigned level, bool shared, unsigned expected) {
        DisplayList dl(memory);
        dl.command(0x9C, 0); dl.command(0x9D, 64); dl.command(0xD2, 3);
        dl.command(0xD4, 0); dl.command(0xD5, 63 | (63 << 10));
        dl.command(0x12, 3u | (3u << 7u) | (1u << 23)); dl.command(0x1E, 1);
        for (unsigned mip = 0; mip < 8; ++mip) {
            dl.command(0xA0 + mip, (texture + mip * 0x4000) & 0xFFFFFF);
            dl.command(0xA8 + mip, 0x040080);
            dl.command(0xB8 + mip, (7 - mip) | ((7 - mip) << 8));
        }
        dl.command(0xC2, (7 << 16) | (shared ? 0 : 0x100) | 1);
        dl.command(0xC3, 4); dl.command(0xC9, 3); dl.command(0xC6, 4);
        dl.command(0xB0, palette & 0xFFFFFF); dl.command(0xB1, 0x080000);
        dl.command(0xC5, 0xFF03); dl.command(0xC4, 32);
        dl.command(0xC8, 1 | (level * 16 << 16)); dl.draw(6, 2); dl.run(ge);
        expect_pixel(memory, &gl, expected);
    };
    for (unsigned level = 0; level < 8; ++level) draw(level, false, colors[level]);
    draw(7, true, colors[0]);
    draw(7, false, colors[7]);
    // Same frame, same base pixels, same palette: only mip 7's VRAM changes.
    memory.store8(texture + 7 * 0x4000, 0x11);
    ge.touch_vram(texture + 7 * 0x4000, 1);
    draw(7, false, 0xFFFFFF);
}

// Decoded textures use fixed-point coordinates and four-bit filter weights.
// The first case has weights 144/48/48/16: blue is 74.5, which truncates to 74.
void decoded_filter_precision() {
    std::cout << "Decoded texture filter precision: GLES3" << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    assert(gl.init()); ge.set_gl(&gl);
    constexpr unsigned texture = vertices + 0x1000;
    const std::array<unsigned, 4> texels = {0x0D110503, 0xF1CD9565, 0x257F3B1F, 0xAD2BD3EF};
    // RGBA8888 rows have a four-texel minimum even when the encoded width is 2.
    for (unsigned i = 0; i < texels.size(); ++i) memory.store32(texture + ((i / 2) * 4 + i % 2) * 4, texels[i]);
    memory.store32(texture + 32, 0xFFFFFFFF);
    struct Case { float u, v; bool linear, clamp; unsigned rgb, alpha; };
    const std::array<Case, 7> cases = {{
        {.75f, .75f, true, false, 0x4A3729, 70},
        {.8f, .9f, true, false, 0x4F3E30, 70},
        {-65.0f / 1024, .75f, true, false, 0x6F6450, 134},
        {-1.0f / 1024, .5f, false, false, 0x110503, 13},
        {-65.0f / 1024, .75f, true, true, 0x2C120A, 19},
        {1025.0f / 1024, .5f, false, false, 0xCD9565, 241},
        {.75f, 205.0f / 256, true, false, 0x4A3729, 70},
    }};
    for (int scale : {1, 2}) {
        assert(gl.set_render_scale(scale) == scale);
        for (bool mips : {false, true}) for (const auto &test : cases) {
            // GE clear keeps the GPU background coherent. A wrong sampled
            // alpha fails the equality test below and leaves this magenta.
            const std::array<float, 6> clear_data = {0, 0, 0, 64, 64, 0};
            for (unsigned i = 0; i < clear_data.size(); ++i)
                memory.store32(vertices + i * 4, std::bit_cast<unsigned>(clear_data[i]));
            DisplayList clear(memory);
            clear.command(0x9C, 0); clear.command(0x9D, 64); clear.command(0xD2, 3);
            clear.command(0xD4, 0); clear.command(0xD5, 63 | (63 << 10));
            clear.command(0x12, (3u << 7) | (1u << 23)); clear.command(0x1E, 0);
            clear.command(0x21, 0); clear.command(0x22, 0); clear.command(0xD3, 0x301);
            clear.command(0x55, 0xFF00FF); clear.command(0x58, 255);
            clear.draw(6, 2); clear.run(ge);
            const std::array<float, 10> data = {test.u, test.v, 0, 0, 0, test.u, test.v, 64, 64, 0};
            for (unsigned i = 0; i < data.size(); ++i)
                memory.store32(vertices + i * 4, std::bit_cast<unsigned>(data[i]));
            DisplayList draw(memory);
            draw.command(0x12, 3u | (3u << 7) | (1u << 23)); draw.command(0x1E, 1);
            draw.command(0xD3, 0); draw.command(0xA0, texture & 0xFFFFFF); draw.command(0xA8, 0x080002);
            draw.command(0xB8, 0x101); draw.command(0xC2, mips ? 0x10000 : 0); draw.command(0xC3, 3);
            draw.command(0xA1, (texture + 32) & 0xFFFFFF); draw.command(0xA9, 0x080001); draw.command(0xB9, 0);
            draw.command(0xC8, 1); // Constant level zero, with/without an uploaded mip.
            draw.command(0xC9, 0x103); draw.command(0xC7, test.clamp ? 0x101 : 0);
            draw.command(0xC6, test.linear ? 0x101 : 0);
            draw.command(0x22, 1); draw.command(0xDB, 2 | (test.alpha << 8) | 0xFF0000);
            const auto downloads = gl.downloads();
            draw.draw(6, 2); draw.run(ge); gl.flush();
            assert(gl.downloads() == downloads);
            expect_pixel(memory, &gl, test.rgb);
        }
    }
}

void surface_filter_precision() {
    std::cout << "GPU surface-window filter precision: GLES3" << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    assert(gl.init()); ge.set_gl(&gl);
    const std::array<unsigned, 4> colors = {0x0D110503, 0xF1CD9565, 0x257F3B1F, 0xAD2BD3EF};
    // The four source pixels occupy a 2x2 window inside a larger GPU surface.
    // Expected bytes include each source format's RGB and stencil precision.
    const std::array<std::array<unsigned, 7>, 4> expected = {{
        {0xFF483525, 0xFF4D3C2B, 0xFF6E644D, 0xFF100400, 0xFF2A1106, 0xFFCE9663, 0xFF483525},
        {0x3F483325, 0x3F4D3B2B, 0x8F6E624D, 0x00100000, 0x002A0E06, 0xFFCE9463, 0x3F483325},
        {0x40473324, 0x404B3A2A, 0x866D654E, 0x00110000, 0x082A0C04, 0xFFCC9966, 0x40473324},
        {0x464A3729, 0x464F3E30, 0x866F6450, 0x0D110503, 0x132C120A, 0xF1CD9565, 0x464A3729},
    }};
    struct Case { float u, v; bool linear, clamp; };
    const std::array<Case, 7> cases = {{{.75f, .75f, true, false}, {.8f, .9f, true, false},
        {-65.0f / 1024, .75f, true, false}, {-1.0f / 1024, .5f, false, false},
        {-65.0f / 1024, .75f, true, true}, {1025.0f / 1024, .5f, false, false},
        {.75f, 205.0f / 256, true, false}}};
    auto clear = [&](unsigned offset, unsigned format, unsigned color, float x0, float y0, float x1, float y1) {
        const std::array<float, 6> data = {x0, y0, 0, x1, y1, 0};
        for (unsigned i = 0; i < data.size(); ++i)
            memory.store32(vertices + i * 4, std::bit_cast<unsigned>(data[i]));
        DisplayList dl(memory);
        dl.command(0x9C, offset); dl.command(0x9D, 64); dl.command(0xD2, format);
        dl.command(0xD4, 0); dl.command(0xD5, 63 | (63 << 10));
        dl.command(0x12, (3u << 7) | (1u << 23)); dl.command(0x1E, 0);
        dl.command(0x21, 0); dl.command(0x22, 0); dl.command(0x24, 0); dl.command(0xD3, 0x301);
        dl.command(0x55, color & 0xFFFFFF); dl.command(0x58, color >> 24);
        dl.draw(6, 2); dl.run(ge);
    };
    for (int scale : {1, 2}) {
        assert(gl.set_render_scale(scale) == scale);
        for (unsigned format : {3u, 0u, 1u, 2u}) {
            clear(0, format, 0xFFFF00FF, 0, 0, 64, 64); // Outside-window sentinel.
            const unsigned bpp = format == 3 ? 4 : 2;
            const unsigned source = 0x04000000 + (9 * 64 + 8) * bpp; // 16-byte aligned in every format.
            for (bool feedback : {false, true}) {
                if (feedback && format != 3) continue;
                const unsigned destination = feedback ? 0 : 0x60000;
                for (unsigned mask : {0x40u, 0xFFu}) for (unsigned i = 0; i < cases.size(); ++i) {
                    const auto &test = cases[i];
                    for (unsigned y = 0; y < 2; ++y) for (unsigned x = 0; x < 2; ++x) {
                        const unsigned color = colors[y * 2 + x];
                        clear(0, format, color & 0xFFFFFF, 8.0f + x, 9.0f + y, 9.0f + x, 10.0f + y);
                        // Update stencil without writing the texture's alpha
                        // attachment. Linear filtering needs all source alpha
                        // bits, even when the final test checks only bit six.
                        DisplayList stencil(memory);
                        stencil.command(0xD3, 0); stencil.command(0x24, 1);
                        stencil.command(0xDC, 1 | ((color >> 24) << 8) | 0xFF0000);
                        stencil.command(0xDD, 0x020202); stencil.command(0xE9, 0);
                        stencil.draw(6, 2); stencil.run(ge);
                    }
                    gl.flush();
                    assert(gl.is_gpu_target(source, format, 64));
                    clear(destination, 3, 0xFFFF00FF, 32, 32, 48, 48);
                    // At 2x choose the same physical tap weights across the
                    // source pixel boundary, preserving its finer sample grid.
                    auto scaled = [scale](float uv) { return (uv < 0 ? uv : uv + scale - 1) / scale; };
                    const float u = scaled(test.u), v = scaled(test.v);
                    const std::array<float, 10> data = {u, v, 32, 32, 0, u, v, 48, 48, 0};
                    for (unsigned j = 0; j < data.size(); ++j)
                        memory.store32(vertices + j * 4, std::bit_cast<unsigned>(data[j]));
                    DisplayList draw(memory);
                    draw.command(0x12, 3u | (3u << 7) | (1u << 23)); draw.command(0x1E, 1);
                    draw.command(0xD3, 0); draw.command(0xA0, source & 0xFFFFFF); draw.command(0xA8, 0x040040);
                    draw.command(0xB8, 0x101); draw.command(0xC2, 0); draw.command(0xC3, format);
                    draw.command(0xC8, 1); draw.command(0xC9, 0x103);
                    draw.command(0xC7, test.clamp ? 0x101 : 0); draw.command(0xC6, test.linear ? 0x101 : 0);
                    draw.command(0x22, 1);
                    draw.command(0xDB, 2 | (((expected[format][i] >> 24) & mask) << 8) | (mask << 16));
                    const auto downloads = gl.downloads(), copies = gl.passes().feedback_copies;
                    draw.draw(6, 2); draw.run(ge); gl.flush();
                    assert(gl.downloads() == downloads);
                    assert(gl.passes().feedback_copies == copies + (feedback ? 1 : 0));
                    expect_pixel(memory, &gl, expected[format][i] & 0xFFFFFF, 0,
                                 0x04000000 + destination + (40 * 64 + 40) * 4);
                }
            }
        }
    }
}

void surface_filter_feedback_order() {
    std::cout << "Filtered GPU feedback ordering: GLES3" << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    assert(gl.init()); ge.set_gl(&gl);
    const std::array<unsigned, 4> colors = {0x110503, 0xCD9565, 0x7F3B1F, 0x2BD3EF};
    for (int scale : {1, 2}) for (bool alias : {false, true}) {
        assert(gl.set_render_scale(scale) == scale);
        for (unsigned i = 0; i < colors.size(); ++i) {
            const float x = 8.0f + (i & 1), y = 9.0f + (i >> 1);
            const std::array<float, 6> data = {x, y, 0, x + 1, y + 1, 0};
            for (unsigned j = 0; j < data.size(); ++j)
                memory.store32(vertices + j * 4, std::bit_cast<unsigned>(data[j]));
            DisplayList clear(memory);
            clear.command(0x9C, 0); clear.command(0x9D, 64); clear.command(0xD2, 3);
            clear.command(0xD4, 0); clear.command(0xD5, 63 | (63 << 10));
            clear.command(0x12, (3u << 7) | (1u << 23)); clear.command(0x1E, 0);
            clear.command(0xD3, 0x301); clear.command(0x55, colors[i]); clear.command(0x58, 255);
            clear.draw(6, 2); clear.run(ge);
        }
        gl.flush();
        const float uv = 1.0f - .25f / scale;
        const std::array<float, 10> data = {uv, uv, alias ? 16.0f : 8.0f, 9, 0,
                                           uv, uv, alias ? 20.0f : 10.0f, 11, 0};
        for (unsigned i = 0; i < data.size(); ++i)
            memory.store32(vertices + i * 4, std::bit_cast<unsigned>(data[i]));
        DisplayList draw(memory);
        draw.command(0x9D, alias ? 128 : 64); draw.command(0xD2, alias ? 0 : 3);
        draw.command(0x12, 3u | (3u << 7) | (1u << 23)); draw.command(0x1E, 1); draw.command(0xD3, 0);
        draw.command(0xA0, (9 * 64 + 8) * 4); draw.command(0xA8, 0x040040);
        draw.command(0xB8, 0x101); draw.command(0xC2, 0); draw.command(0xC3, 3);
        draw.command(0xC6, 0x101); draw.command(0xC7, 0x101); draw.command(0xC8, 1);
        draw.command(0xC9, alias ? 0 : 0x100); draw.command(0x55, 0x808080);
        draw.command(0x22, alias ? 0 : 1); draw.command(0xDB, 0xFFFF02);
        const auto downloads = gl.downloads(), copies = gl.passes().feedback_copies;
        const auto reinterprets = gl.passes().reinterprets;
        draw.draw(6, 2); draw.draw(6, 2); draw.run(ge); gl.flush();
        assert(gl.downloads() == downloads);
        if (!alias && gl.passes().feedback_copies != copies + 2)
            std::cerr << "filtered feedback copies: " << gl.passes().feedback_copies - copies << '\n';
        assert(gl.passes().feedback_copies == copies + (alias ? 0 : 2));
        // First filtered/modulated write is (20,27,37). The overlapping
        // primitive must resample it, producing (10,13,18), not the first result.
        if (!alias) expect_pixel(memory, &gl, 0x120D0A, 0, 0x04000000 + (9 * 64 + 8) * 4);
        else {
            // First RGB565 write is 0x20C2. The aliased RGBA view sees
            // (194,32,194); resampling/modulation must then write 0x608C.
            assert(gl.passes().reinterprets > reinterprets);
            constexpr unsigned at = 0x04000000 + (9 * 128 + 16) * 2;
            gl.prepare_vram_read(at, 2);
            assert(memory.load16(at) == 0x608C);
        }
    }
}

void surface_filter_scale_and_lod() {
    std::cout << "GPU surface filter scale and LOD: GLES3" << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    assert(gl.init()); ge.set_gl(&gl); assert(gl.set_render_scale(2) == 2);
    struct Vertex { unsigned rgba; float x, y, z; };
    const std::array<Vertex, 4> source = {{{0xFF000000, 8, 9, 0}, {0xFF000010, 9, 9, 0},
                                         {0xFF000000, 8, 11, 0}, {0xFF000010, 9, 11, 0}}};
    for (unsigned i = 0; i < source.size(); ++i) {
        memory.store32(vertices + i * 16, source[i].rgba);
        memory.store32(vertices + i * 16 + 4, std::bit_cast<unsigned>(source[i].x));
        memory.store32(vertices + i * 16 + 8, std::bit_cast<unsigned>(source[i].y));
        memory.store32(vertices + i * 16 + 12, 0);
    }
    DisplayList gradient(memory);
    gradient.command(0x9C, 0); gradient.command(0x9D, 64); gradient.command(0xD2, 3);
    gradient.command(0xD4, 0); gradient.command(0xD5, 63 | (63 << 10));
    gradient.command(0x12, (7u << 2) | (3u << 7) | (1u << 23)); gradient.command(0x50, 1);
    gradient.draw(4, 4); gradient.run(ge); gl.flush();
    constexpr unsigned source_address = 0x04000000 + (9 * 64 + 8) * 4;
    assert(gl.is_gpu_target(source_address, 3, 64));
    auto sample_source = [&](float u, unsigned filter, int bias, unsigned expected) {
        const std::array<float, 10> data = {u, .5f, 32, 32, 0, u, .5f, 48, 48, 0};
        for (unsigned i = 0; i < data.size(); ++i)
            memory.store32(vertices + i * 4, std::bit_cast<unsigned>(data[i]));
        DisplayList draw(memory);
        draw.command(0x9C, 0x60000); draw.command(0x12, 3u | (3u << 7) | (1u << 23));
        draw.command(0x1E, 1); draw.command(0xA0, source_address & 0xFFFFFF); draw.command(0xA8, 0x040040);
        draw.command(0xB8, 0x100); draw.command(0xC2, 0); draw.command(0xC3, 3);
        draw.command(0xC7, 0x101); draw.command(0xC9, 0x103); draw.command(0xC6, filter);
        draw.command(0xC8, 1 | ((static_cast<unsigned>(bias) & 255) << 16));
        draw.command(0x55, 0xFFFFFF); draw.command(0x58, 255);
        const auto downloads = gl.downloads();
        draw.draw(6, 2); draw.run(ge); gl.flush();
        assert(gl.downloads() == downloads);
        expect_pixel(memory, &gl, expected, 0, 0x04060000 + (40 * 64 + 40) * 4);
    };
    // Two distinct samples inside one logical PSP pixel survive 2x sampling.
    sample_source(.25f, 0, 0, 4); sample_source(.75f, 0, 0, 12);
    // No mip chain is needed for LOD mode/bias to choose min versus mag.
    for (int bias : {-16, 16}) for (unsigned filter : {0u, 1u, 0x100u, 0x101u}) {
        const bool linear = (filter & (bias > 0 ? 1u : 0x100u)) != 0;
        sample_source(.375f, filter, bias, linear ? 6 : 4);
    }
}

// Minification and magnification are independent GE filter settings. Use a
// striped texture with known fractional sample positions in both regimes.
void texture_filters() {
    std::cout << "Texture min/mag filter tests: GLES3" << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    assert(gl.init()); ge.set_gl(&gl);
    constexpr std::uint32_t texture = vertices + 0x1000;
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 64; ++x)
            memory.store32(texture + (y * 64 + x) * 4, x & 1 ? 0xFFFFFFFFu : 0xFF000000u);
    for (bool minify : {true, false}) {
        const std::array<float, 10> data = {0, .5f, 0, 0, 0, minify ? 128.0f : 32.0f, .5f, 64, 64, 0};
        for (unsigned i = 0; i < data.size(); ++i)
            memory.store32(vertices + i * 4, std::bit_cast<std::uint32_t>(data[i]));
        for (unsigned wrap : {0u, 1u, 0x100u, 0x101u})
        for (unsigned filter : {0u, 1u, 0x100u, 0x101u, 0u}) {
            DisplayList draw(memory);
            draw.command(0x9C, 0); draw.command(0x9D, 64); draw.command(0xD2, 3);
            draw.command(0xD4, 0); draw.command(0xD5, 63 | (63 << 10));
            draw.command(0x12, 3u | (3u << 7u) | (1u << 23)); draw.command(0x1E, 1);
            draw.command(0xA0, texture & 0xFFFFFF); draw.command(0xA8, 0x080040);
            draw.command(0xB8, 0x106); draw.command(0xC3, 3); draw.command(0xC9, 3);
            draw.command(0xC7, wrap);
            draw.command(0xC6, filter); draw.draw(6, 2); draw.run(ge);
            const bool linear = (filter & (minify ? 1u : 0x100u)) != 0;
            const unsigned channel = minify ? (linear && !(wrap & 1) ? 127 : 255) : (linear ? 63 : 0);
            expect_pixel(memory, &gl, channel * 0x010101u);
        }
    }
}

// Sampling a GPU-rendered 16-bit target must see its packed channel precision,
// even when no CPU readback occurred between drawing and sampling it.
void render_target_precision(bool use_gl) {
    std::cout << "Render-target precision tests: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    auto *backend = use_gl ? &gl : nullptr;
    for (unsigned format : {0u,1u,2u})
    for (unsigned color : {0x3264FAu,0x102008u,0xFFFFFFu,0u}) for (bool clear : {false,true}) {
        const unsigned source_offset=format*0x10000u;
        const std::array<float,6> source_vertices={0,0,0,64,64,0};
        for (unsigned i=0;i<source_vertices.size();++i)
            memory.store32(vertices+i*4u,std::bit_cast<std::uint32_t>(source_vertices[i]));
        DisplayList first(memory);
        first.command(0x9C,source_offset);first.command(0x9D,64);first.command(0xD2,format);
        first.command(0xD4,0);first.command(0xD5,63|(63<<10));
        first.command(0x12,(3u<<7)|(1u<<23));first.command(0x1E,0);
        first.command(0x55,color);first.command(0x58,255);first.command(0xD3,clear?0x101:0);
        first.draw(6,2);first.run(ge);
        if (backend) {
            backend->flush();
            assert(backend->is_gpu_target(0x04000000u+source_offset,format,64));
        }
        const auto downloads_before=backend?backend->downloads():0;
        const std::array<float,15> textured_vertices={8,8,8,8,0,56,8,56,8,0,32,56,32,56,0};
        for (unsigned i=0;i<textured_vertices.size();++i)
            memory.store32(vertices+i*4u,std::bit_cast<std::uint32_t>(textured_vertices[i]));
        DisplayList second(memory);
        second.command(0x9C,0x60000);second.command(0xD2,3);second.command(0xD3,0);
        second.command(0x12,3u|(3u<<7)|(1u<<23));second.command(0x1E,1);
        second.command(0xA0,source_offset);second.command(0xA8,0x040040);
        second.command(0xB8,0x606);second.command(0xC3,format);second.command(0xC9,3);second.command(0xC6,0);
        second.draw();second.run(ge);
        if (backend) {backend->flush();assert(backend->downloads()==downloads_before);}
        auto expand=[](unsigned value,unsigned bits){
            const unsigned packed=value>>(8-bits);
            return bits==4 ? packed*17 : bits==5 ? (packed<<3)|(packed>>2) : (packed<<2)|(packed>>4);
        };
        const unsigned bits=format==2?4:5;
        const unsigned expected=expand(color&255,bits)|(expand((color>>8)&255,format==0?6:bits)<<8)|(expand((color>>16)&255,bits)<<16);
        expect_pixel(memory,backend,expected,0,sample+0x60000);
    }
}

// PSP depth storage compares integer 16-bit values, including fractional
// drawing-space depths that collapse to the same integer after rasterization.
void integer_depth(bool use_gl) {
    std::cout << "Integer depth tests: " << (use_gl ? "GLES3" : "software") << std::endl;
    psprecomp::GuestMemory memory;
    pspweb::GeGl gl(memory);
    pspweb::Ge ge(memory);
    if (use_gl) { assert(gl.init()); ge.set_gl(&gl); }
    auto *backend = use_gl ? &gl : nullptr;
    auto geometry = [&](float z) {
        if (backend) backend->flush();
        const std::array<float, 9> data = {8, 8, z, 56, 8, z, 32, 56, z};
        for (unsigned i=0; i<data.size(); ++i)
            memory.store32(vertices + i*4u, std::bit_cast<std::uint32_t>(data[i]));
    };
    DisplayList setup(memory);
    setup.command(0x9C, 0); setup.command(0x9D, 64); setup.command(0xD2, 3);
    setup.command(0x9E, 0x100000); setup.command(0x9F, 64);
    setup.command(0xD4, 0); setup.command(0xD5, 63 | (63 << 10));
    setup.command(0x12, (3u << 7u) | (1u << 23u));
    setup.command(0x23, 1); setup.command(0xE7, 0); setup.command(0x58, 255);
    setup.run(ge);
    for (float base : {0.0f, 100.0f, 32767.0f, 65534.0f}) {
        geometry(base + .25f);
        DisplayList first(memory);
        first.command(0x55, 0x0000FF); first.command(0xDE, 1);
        first.draw(); first.run(ge);
        expect_pixel(memory, backend, 0x0000FF);
        geometry(base + .75f);
        DisplayList equal(memory);
        equal.command(0x55, 0x00FF00); equal.command(0xDE, 2);
        equal.draw(); equal.run(ge);
        expect_pixel(memory, backend, 0x00FF00);
        DisplayList greater(memory);
        greater.command(0x55, 0xFF0000); greater.command(0xDE, 6);
        greater.draw(); greater.run(ge);
        expect_pixel(memory, backend, 0x00FF00);
        geometry(base + 1.25f);
        DisplayList next(memory);
        next.command(0x55, 0xFF0000); next.command(0xDE, 6);
        next.draw(); next.run(ge);
        expect_pixel(memory, backend, 0xFF0000);
    }
}

struct GlContext {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    GlContext() {
#if defined(_WIN32)
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#else
        auto get_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        display = get_display ? get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                              : eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif
        EGLint major, minor, count;
        assert(display != EGL_NO_DISPLAY && eglInitialize(display, &major, &minor));
        assert(eglBindAPI(EGL_OPENGL_ES_API));
        const EGLint attributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                     EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig config;
        assert(eglChooseConfig(display, attributes, &config, 1, &count) && count == 1);
        for (EGLint channel : {EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE, EGL_ALPHA_SIZE}) {
            EGLint bits = 0;
            assert(eglGetConfigAttrib(display, config, channel, &bits) && bits >= 8);
        }
        const EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
        const EGLint surface_attributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        surface = eglCreatePbufferSurface(display, config, surface_attributes);
        assert(context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE);
        assert(eglMakeCurrent(display, surface, surface, context));
    }
    ~GlContext() {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(display, surface); eglDestroyContext(display, context); eglTerminate(display);
    }
};
}
int main() {
    float24_rows();
    viewport_arithmetic();
    viewport_pixels(false);
    float24_uv_stages(false);
    float24_matrix_composition(false);
    texture_row_alignment(false);
    triangle_sampling(false);
    line_coverage(false);
    line_attributes(false);
    lighting_colors(false);
    lighting_components(false);
    texture_coordinates(false);
    fog_depth(false);
    integer_depth(false);
    doubled_destination_alpha(false);
    render_target_precision(false);
    ordered_dither(false);
    rgb565_destination_alpha(false);
    exact_blend_rounding(false);
    interpolated_color_precision(false);
    texture_function_precision(false);
    GlContext context;
    viewport_pixels(true);
    float24_uv_stages(true);
    float24_matrix_composition(true);
    texture_row_alignment(true);
    staged_bilinear_precision();
    affine_color_perspective_uv();
    triangle_sampling(true);
    line_coverage(true);
    line_attributes(true);
    line_texture_lod();
    lighting_colors(true);
    lighting_components(true);
    texture_coordinates(true);
    fog_depth(true);
    integer_depth(true);
    doubled_destination_alpha(true);
    render_target_precision(true);
    ordered_dither(true);
    rgb565_destination_alpha(true);
    exact_blend_rounding(true);
    interpolated_color_precision(true);
    interpolated_secondary_precision();
    texture_function_precision(true);
    decoded_filter_precision();
    surface_filter_precision();
    surface_filter_feedback_order();
    surface_filter_scale_and_lod();
    texture_filters();
    fixed_uv_planes();
    texture_mipmaps();
    mipmap_clut_and_vram();
    presentation_sampler_state();
    ordered_blend_gpu();
    std::cout << "GE shared software/GLES3 tests and GLES3 filter/mipmap tests passed\n";
}
