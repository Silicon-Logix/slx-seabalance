// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "renderer.hpp"
#include "gl_api.hpp"
#include "session.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>
#include <system_error>

namespace slx::sb {
namespace {
using Vec3 = std::array<float, 3>;
using Quat = std::array<float, 4>;
struct Mat {
    float m[16]{};
};
Vec3 cross(Vec3 a, Vec3 b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float dot(Vec3 a, Vec3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 unit(Vec3 a)
{
    const float d = std::sqrt(dot(a, a));
    return {a[0] / d, a[1] / d, a[2] / d};
}
Mat camera_matrix(Vec3 eye, Vec3 target, float aspect)
{
    const auto f = unit({target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]});
    const auto s = unit(cross(f, {0, 1, 0})), u = cross(s, f);
    const Mat view{{s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0,
                    -dot(s, eye), -dot(u, eye), dot(f, eye), 1}};
    constexpr float near_plane = .10f, far_plane = 350.f;
    const float a = 1 / std::tan(.35f);
    const Mat projection{{a / aspect, 0, 0, 0, 0, a, 0, 0, 0, 0,
                          -(near_plane + far_plane) / (far_plane - near_plane), -1, 0, 0,
                          -2 * near_plane * far_plane / (far_plane - near_plane), 0}};
    Mat result;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                result.m[column * 4 + row] += projection.m[k * 4 + row] * view.m[column * 4 + k];
    return result;
}

constexpr const char *ship_vertex = R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
uniform mat4 vp, model;
out vec3 world, nrm;
out vec2 texcoord;
void main() {
    vec4 p = model * vec4(position, 1);
    world = p.xyz;
    nrm = mat3(transpose(inverse(model))) * normal;
    texcoord = uv;
    gl_Position = vp * p;
})";
constexpr const char *ship_fragment = R"(#version 330 core
in vec3 world, nrm;
in vec2 texcoord;
uniform vec3 camera, surface;
uniform vec4 base;
uniform sampler2D artwork;
uniform int textured;
out vec4 pixel;
void main() {
    vec4 albedo = base * (textured != 0 ? texture(artwork, texcoord) : vec4(1));
    if (albedo.a < surface.z) discard;
    vec3 n = normalize(nrm);
    if (!gl_FrontFacing) n = -n;
    vec3 sun = normalize(vec3(.30, .83, .55));
    vec3 view = normalize(camera - world);
    vec3 halfdir = normalize(view + sun);
    float metallic = surface.x, roughness = surface.y;
    float diffuse = max(dot(n, sun), 0.);
    float highlight = pow(max(0., dot(n, halfdir)), mix(85., 12., roughness));
    vec3 light = albedo.rgb * (vec3(.37,.45,.54) + diffuse * vec3(1.16,1.04,.89));
    light += mix(vec3(.055), albedo.rgb * .43, metallic) * highlight;
    light += albedo.rgb * max(0., dot(n, normalize(vec3(-.8,.2,-.65)))) * .19;
    pixel = vec4(pow(clamp(light, 0., 1.), vec3(1./2.2)), albedo.a);
})";
constexpr const char *sky_vertex = R"(#version 330 core
out vec2 uv;
void main() {
    uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
})";
constexpr const char *sky_fragment = R"(#version 330 core
in vec2 uv;
uniform float dry, storm;
out vec4 pixel;
void main() {
    vec3 top = mix(mix(vec3(.42,.64,.77),vec3(.20,.29,.39),storm), vec3(.14,.22,.33), dry);
    vec3 horizon = mix(mix(vec3(.79,.88,.91),vec3(.62,.68,.72),storm), vec3(.51,.63,.74), dry);
    vec3 c = mix(horizon, top, smoothstep(.15, 1.0, uv.y));
    c += vec3(.07,.054,.032) * exp(-3.4*length(uv-vec2(.69,.56))) * (1.0-dry) * (1.0-.8*storm);
    pixel = vec4(c, 1.);
})";
constexpr const char *water_vertex = R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=3) in vec3 wave_a;
layout(location=4) in vec3 wave_b;
uniform mat4 vp;
uniform int illustrative;
uniform float weight[6];
uniform vec3 scenario, at_sample;
out vec3 world;
void main() {
    world = position;
    if (illustrative != 0) {
        world.y += dot(wave_a, vec3(weight[0],weight[1],weight[2])) +
                   dot(wave_b, vec3(weight[3],weight[4],weight[5]));
    } else {
        float p = dot(position.xz, vec2(-cos(scenario.x),sin(scenario.x)));
        float k = scenario.y;
        float t = at_sample.x;
        world.y += at_sample.y*cos(k*p) + scenario.z * .36 * sin(k*p)*cos(at_sample.z*t);
        world.y += scenario.z * .055 * sin(k*p + position.x*.19) * sin(1.7*at_sample.z*t);
    }
    gl_Position = vp * vec4(world, 1.);
})";
constexpr const char *water_fragment = R"(#version 330 core
in vec3 world;
uniform vec3 camera, at_sample, framebuffer;
uniform float storm;
out vec4 pixel;
void main() {
    vec3 n = normalize(cross(dFdx(world), dFdy(world)));
    if (n.y < 0.) n = -n;
    vec3 v = normalize(camera - world);
    float ripple = sin(world.x*.83+world.z*.36-at_sample.x*1.3) *
                   sin(world.x*.21-world.z*.95+at_sample.x*1.6);
    n = normalize(n + vec3(.046*ripple,0.,.05*sin(world.z*1.1-world.x*.49+at_sample.x)));
    float fresnel = pow(1. - max(0., dot(n,v)), 3.0);
    float dots = max(0., dot(n, normalize(vec3(.42,.83,.40) + v)));
    vec3 c = mix(mix(vec3(.075,.30,.40),vec3(.06,.16,.23),storm),
                 mix(vec3(.46,.66,.76),vec3(.31,.45,.55),storm), fresnel);
    c += vec3(.062,.092,.09) * ripple + vec3(.35,.33,.27)*pow(dots,75.);
    float crest = smoothstep(.11,.43,length(n.xz)) * smoothstep(.2,.85,ripple);
    c = mix(c,vec3(.74,.83,.86),crest * (.13+.30*storm));
    vec2 uv = gl_FragCoord.xy / framebuffer.xy;
    vec3 sky = mix(mix(vec3(.79,.88,.91),vec3(.62,.68,.72),storm),
                   mix(vec3(.42,.64,.77),vec3(.20,.29,.39),storm),smoothstep(.15,1.,uv.y));
    sky += vec3(.07,.054,.032) * exp(-3.4*length(uv-vec2(.69,.56))) * (1.0-.8*storm);
    float fog = smoothstep(25.,74.,length(camera-world));
    pixel = vec4(mix(c,sky,fog),1.);
})";

GLuint shader(GLenum kind, const char *code)
{
    const auto id = gl::CreateShader(kind);
    gl::ShaderSource(id, 1, &code, nullptr);
    gl::CompileShader(id);
    GLint success{};
    gl::GetShaderiv(id, GL_COMPILE_STATUS, &success);
    if (success)
        return id;
    char error[2048]{};
    gl::GetShaderInfoLog(id, sizeof(error), nullptr, error);
    gl::DeleteShader(id);
    throw std::runtime_error(std::string("SeaBalance GLSL - ") + error);
}

struct Program {
    GLuint id{};
    Program(const char *v, const char *f)
    {
        const auto vertex = shader(GL_VERTEX_SHADER, v);
        GLuint fragment{};
        try {
            fragment = shader(GL_FRAGMENT_SHADER, f);
        } catch (...) {
            gl::DeleteShader(vertex);
            throw;
        }
        id = gl::CreateProgram();
        gl::AttachShader(id, vertex);
        gl::AttachShader(id, fragment);
        gl::LinkProgram(id);
        gl::DeleteShader(vertex);
        gl::DeleteShader(fragment);
        GLint success{};
        gl::GetProgramiv(id, GL_LINK_STATUS, &success);
        if (!success) {
            char error[2048]{};
            gl::GetProgramInfoLog(id, sizeof(error), nullptr, error);
            gl::DeleteProgram(id);
            throw std::runtime_error(std::string("SeaBalance OpenGL program - ") + error);
        }
    }
    ~Program() { gl::DeleteProgram(id); }
    GLint at(const char *name) const { return gl::GetUniformLocation(id, name); }
};

std::vector<uint8_t> file_bytes(const fs::path &file)
{
    const auto size = fs::file_size(file);
    if (size == 0 || size > 32 * 1024 * 1024)
        throw std::runtime_error("Asset size out of range - " + file.filename().string());
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    std::ifstream in(file, std::ios::binary);
    if (!in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read asset - " + file.filename().string());
    return bytes;
}

GLuint texture(std::span<const uint8_t> bytes, bool srgb)
{
    int w{}, h{}, channels{};
    if (bytes.empty() || !stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels) ||
        w <= 0 || w > 4096 || h <= 0 || h > 4096)
        throw std::runtime_error("Invalid embedded asset texture dimensions");
    auto *rgba = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!rgba)
        throw std::runtime_error("Cannot decode embedded texture");
    GLuint id{};
    gl::GenTextures(1, &id);
    gl::BindTexture(GL_TEXTURE_2D, id);
    gl::TexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    stbi_image_free(rgba);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::GenerateMipmap(GL_TEXTURE_2D);
    return id;
}

struct Geometry {
    GLuint vao{}, vertices{}, indices{};
    GLsizei count{};
    Geometry(std::span<const float> points, std::span<const uint32_t> faces, int layout)
    {
        gl::GenVertexArrays(1, &vao);
        gl::GenBuffers(1, &vertices);
        gl::GenBuffers(1, &indices);
        gl::BindVertexArray(vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, vertices);
        gl::BufferData(GL_ARRAY_BUFFER, points.size_bytes(), points.data(), GL_STATIC_DRAW);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, faces.size_bytes(), faces.data(), GL_STATIC_DRAW);
        auto attribute = [layout](GLuint id, int size, int offset) {
            gl::EnableVertexAttribArray(id);
            gl::VertexAttribPointer(id, size, GL_FLOAT, GL_FALSE, layout * sizeof(float),
                                    reinterpret_cast<const void *>(static_cast<size_t>(offset) * sizeof(float)));
        };
        attribute(0, 3, 0);
        if (layout == 8) {
            attribute(1, 3, 3);
            attribute(2, 2, 6);
        }
        if (layout == 9) {
            attribute(3, 3, 3);
            attribute(4, 3, 6);
        }
        count = static_cast<GLsizei>(faces.size());
    }
    ~Geometry()
    {
        gl::DeleteBuffers(1, &indices);
        gl::DeleteBuffers(1, &vertices);
        gl::DeleteVertexArrays(1, &vao);
    }
    void draw() const
    {
        gl::BindVertexArray(vao);
        gl::DrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    }
};

struct Draw {
    std::unique_ptr<Geometry> geometry;
    cgltf_node *node{};
    GLuint image{};
    std::array<float, 4> base{1, 1, 1, 1};
    float metallic{}, roughness{.75f}, cutoff{.01f};
    bool transparent{}, double_sided{};
    Mat world;
};

struct Asset {
    std::vector<uint8_t> bytes;
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data{nullptr, cgltf_free};
    std::vector<Draw> draws;
    std::vector<GLuint> images;
    struct Rest { Vec3 translation, scale; Quat rotation; };
    std::vector<Rest> neutral;

    explicit Asset(const fs::path &path, bool ocean = false) : bytes(file_bytes(path))
    {
        const cgltf_options opt{};
        cgltf_data *parsed{};
        if (cgltf_parse(&opt, bytes.data(), bytes.size(), &parsed) != cgltf_result_success)
            throw std::runtime_error("Cannot parse " + path.filename().string());
        data.reset(parsed);
        if (data->file_type != cgltf_file_type_glb || data->buffers_count != 1 || data->buffers[0].uri ||
            cgltf_load_buffers(&opt, data.get(), nullptr) != cgltf_result_success ||
            cgltf_validate(data.get()) != cgltf_result_success || data->nodes_count > 2048)
            throw std::runtime_error("Invalid or external GLB resource - " + path.filename().string());
        try {
            for (size_t i = 0; i < data->textures_count; ++i) {
                auto *image = data->textures[i].image;
                if (!image || image->uri || !image->buffer_view)
                    throw std::runtime_error("GLB texture must be embedded");
                images.push_back(texture({cgltf_buffer_view_data(image->buffer_view), image->buffer_view->size}, true));
            }
            for (size_t i = 0; i < data->nodes_count; ++i) {
                auto &node = data->nodes[i];
                neutral.push_back({{node.translation[0], node.translation[1], node.translation[2]},
                    {node.scale[0], node.scale[1], node.scale[2]},
                    {node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]}});
                if (!node.mesh)
                    continue;
                for (size_t p = 0; p < node.mesh->primitives_count; ++p)
                    mesh(node, node.mesh->primitives[p], ocean);
            }
        } catch (...) {
            gl::DeleteTextures(static_cast<GLsizei>(images.size()), images.data());
            throw;
        }
    }
    ~Asset() { gl::DeleteTextures(static_cast<GLsizei>(images.size()), images.data()); }

    static const cgltf_accessor *attribute(const cgltf_attribute *a, size_t count, cgltf_attribute_type type)
    {
        for (size_t i = 0; i < count; ++i)
            if (a[i].type == type && a[i].index == 0)
                return a[i].data;
        return nullptr;
    }
    void mesh(cgltf_node &node, const cgltf_primitive &p, bool ocean)
    {
        const auto *pos = attribute(p.attributes, p.attributes_count, cgltf_attribute_type_position);
        const auto *norm = attribute(p.attributes, p.attributes_count, cgltf_attribute_type_normal);
        const auto *uv = attribute(p.attributes, p.attributes_count, cgltf_attribute_type_texcoord);
        if (!pos || pos->count > 1000000 || p.type != cgltf_primitive_type_triangles ||
            (ocean && p.targets_count != 6))
            throw std::runtime_error("Unsupported SeaBalance GLB primitive");
        std::array<const cgltf_accessor *, 6> targets{};
        if (ocean) {
            for (size_t i = 0; i < targets.size(); ++i) {
                const auto &t = p.targets[i];
                targets[i] = attribute(t.attributes, t.attributes_count, cgltf_attribute_type_position);
                if (!targets[i] || targets[i]->count != pos->count)
                    throw std::runtime_error("Missing ocean position morph target");
            }
        }
        const int stride = ocean ? 9 : 8;
        std::vector<float> vertices(pos->count * stride);
        for (size_t i = 0; i < pos->count; ++i) {
            auto *v = vertices.data() + i * stride;
            cgltf_accessor_read_float(pos, i, v, 3);
            if (ocean) {
                for (size_t j = 0; j < targets.size(); ++j) {
                    float delta[3]{};
                    cgltf_accessor_read_float(targets[j], i, delta, 3);
                    v[3 + j] = delta[1];
                }
            } else {
                v[4] = 1.;
                if (norm) cgltf_accessor_read_float(norm, i, v + 3, 3);
                if (uv) cgltf_accessor_read_float(uv, i, v + 6, 2);
            }
        }
        const auto count = p.indices ? p.indices->count : pos->count;
        if (count > 3000000)
            throw std::runtime_error("GLB primitive has too many indices");
        std::vector<uint32_t> indices(count);
        for (size_t i = 0; i < count; ++i)
            indices[i] = static_cast<uint32_t>(p.indices ? cgltf_accessor_read_index(p.indices, i) : i);
        Draw d;
        d.node = &node;
        d.geometry = std::make_unique<Geometry>(vertices, indices, stride);
        if (p.material) {
            const auto &mat = *p.material;
            const auto &pbr = mat.pbr_metallic_roughness;
            std::copy_n(pbr.base_color_factor, 4, d.base.begin());
            d.metallic = pbr.metallic_factor;
            d.roughness = pbr.roughness_factor;
            d.cutoff = mat.alpha_mode == cgltf_alpha_mode_mask ? mat.alpha_cutoff : .01f;
            d.transparent = mat.alpha_mode == cgltf_alpha_mode_blend;
            d.double_sided = mat.double_sided;
            if (const auto *tex = pbr.base_color_texture.texture)
                d.image = images.at(static_cast<size_t>(tex - data->textures));
        }
        draws.push_back(std::move(d));
    }
    cgltf_node &binding(std::string_view name) const
    {
        cgltf_node *found{};
        for (size_t i = 0; i < data->nodes_count; ++i) {
            auto &n = data->nodes[i];
            if (!n.name || n.name != name)
                continue;
            if (found)
                throw std::runtime_error("Duplicate asset binding - " + std::string(name));
            found = &n;
        }
        if (!found)
            throw std::runtime_error("Missing asset binding - " + std::string(name));
        return *found;
    }
    void rest()
    {
        for (size_t i = 0; i < data->nodes_count; ++i) {
            auto &n = data->nodes[i];
            std::copy(neutral[i].rotation.begin(), neutral[i].rotation.end(), n.rotation);
            std::copy(neutral[i].translation.begin(), neutral[i].translation.end(), n.translation);
            std::copy(neutral[i].scale.begin(), neutral[i].scale.end(), n.scale);
        }
    }
};

void rotation(cgltf_node &n, Vec3 axis, double angle)
{
    const float s = static_cast<float>(std::sin(angle * .5)), w = static_cast<float>(std::cos(angle * .5));
    const float x = axis[0] * s, y = axis[1] * s, z = axis[2] * s;
    const auto [a, b, c, d] = Quat{n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]};
    n.rotation[0] = d * x + a * w + b * z - c * y;
    n.rotation[1] = d * y - a * z + b * w + c * x;
    n.rotation[2] = d * z + a * y - b * x + c * w;
    n.rotation[3] = d * w - a * x - b * y - c * z;
}

float key(const cgltf_accessor *input, size_t i)
{
    float t{};
    cgltf_accessor_read_float(input, i, &t, 1);
    return t;
}

void animate(Asset &asset, std::string_view name, float time)
{
    const cgltf_animation *animation{};
    asset.rest();
    for (size_t i = 0; i < asset.data->animations_count; ++i) {
        const auto &a = asset.data->animations[i];
        if (a.name && a.name == name)
            animation = &a;
    }
    if (!animation)
        throw std::runtime_error("Missing GLB clip - " + std::string(name));
    for (size_t i = 0; i < animation->channels_count; ++i) {
        const auto &channel = animation->channels[i];
        const auto &s = *channel.sampler;
        if (s.input->count == 0 || s.interpolation == cgltf_interpolation_type_cubic_spline)
            throw std::runtime_error("Unsupported GLB animation interpolation");
        size_t left = 0, right = s.input->count - 1;
        while (left + 1 < right) {
            const auto mid = (left + right) / 2;
            if (key(s.input, mid) <= time) left = mid; else right = mid;
        }
        const float t0 = key(s.input, left), t1 = key(s.input, right);
        const float u = s.interpolation == cgltf_interpolation_type_step ?
            (time >= t1 ? 1.f : 0.f) : std::clamp((time - t0) / std::max(1e-6f, t1 - t0), 0.f, 1.f);
        auto &n = *channel.target_node;
        float *out = nullptr;
        int count = 3;
        switch (channel.target_path) {
        case cgltf_animation_path_type_rotation: out = n.rotation; count = 4; break;
        case cgltf_animation_path_type_translation: out = n.translation; break;
        case cgltf_animation_path_type_scale: out = n.scale; break;
        default: throw std::runtime_error("Unexpected vessel clip channel");
        }
        Quat a{}, b{};
        cgltf_accessor_read_float(s.output, left, a.data(), count);
        cgltf_accessor_read_float(s.output, right, b.data(), count);
        if (count == 4) {
            float cosine = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
            if (cosine < 0) {
                for (auto &v : b) v = -v;
                cosine = -cosine;
            }
            const float theta = std::acos(std::clamp(cosine, 0.f, 1.f));
            const float lo = theta < .001f ? 1 - u : std::sin((1-u)*theta)/std::sin(theta);
            const float hi = theta < .001f ? u : std::sin(u*theta)/std::sin(theta);
            float length{};
            for (int j = 0; j < count; ++j) {
                out[j] = lo*a[j] + hi*b[j];
                length += out[j]*out[j];
            }
            for (int j = 0; j < count; ++j) out[j] /= std::sqrt(length);
        } else {
            for (int j = 0; j < count; ++j) out[j] = (1-u)*a[j] + u*b[j];
        }
    }
}

struct Surface {
    GLuint fbo{}, color{}, depth{};
    int width{}, height{};
    ~Surface()
    {
        gl::DeleteFramebuffers(1, &fbo);
        gl::DeleteTextures(1, &color);
        gl::DeleteRenderbuffers(1, &depth);
    }
    void target(int w, int h)
    {
        if (!fbo) {
            gl::GenFramebuffers(1, &fbo);
            gl::GenTextures(1, &color);
            gl::GenRenderbuffers(1, &depth);
        }
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
        if (width == w && height == h)
            return;
        width = w; height = h;
        gl::BindTexture(GL_TEXTURE_2D, color);
        gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        gl::BindRenderbuffer(GL_RENDERBUFFER, depth);
        gl::RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Cannot allocate the viewport framebuffer");
    }
};

std::unique_ptr<Geometry> sea_grid()
{
    constexpr int cells = 112;
    constexpr float width = 235;
    std::vector<float> points;
    std::vector<uint32_t> indices;
    for (int z = 0; z <= cells; ++z)
        for (int x = 0; x <= cells; ++x) {
            points.push_back(width * (x / static_cast<float>(cells) - .5f));
            points.push_back(0.f);
            points.push_back(width * (z / static_cast<float>(cells) - .5f));
        }
    for (uint32_t z = 0; z < cells; ++z)
        for (uint32_t x = 0; x < cells; ++x) {
            const uint32_t a = z * (cells+1) + x, b = a + cells + 1;
            indices.insert(indices.end(), {a,b,a+1,a+1,b,b+1});
        }
    return std::make_unique<Geometry>(points, indices, 3);
}

bool presentation_files_exist(const fs::path &root, const nlohmann::json &manifest)
{
    std::error_code error;
    for (const char *key : {"animated_file", "component_file", "ocean_visual_file"}) {
        const auto field = manifest.find(key);
        if (field == manifest.end() || !field->is_string() ||
            !fs::is_regular_file(root / field->get_ref<const std::string &>(), error))
            return false;
    }
    return fs::is_regular_file(root / "ocean_visual_contract.json", error);
}

} // namespace

struct Renderer::Impl {
    fs::path directory;
    nlohmann::json manifest;
    Program ship{ship_vertex, ship_fragment}, sky{sky_vertex, sky_fragment}, water{water_vertex,water_fragment};
    Asset live;
    std::unique_ptr<Asset> recorded_clips, component, demo_ocean;
    std::unique_ptr<Geometry> grid{sea_grid()};
    std::array<Surface, 2> surfaces;
    cgltf_node *body{}, *port{}, *starboard{};
    Vec3 fin_axis{};
    GLuint sky_vao{}, logo{};
    std::array<float,3> ocean_omega{};
    bool presentation_available{};
    const GLint ship_vp = ship.at("vp"), ship_model = ship.at("model"), ship_camera = ship.at("camera"),
                ship_base = ship.at("base"), ship_surface = ship.at("surface"), ship_textured = ship.at("textured");

    explicit Impl(const fs::path &root)
        : directory(root), manifest(read_json(root / "asset_manifest.json")),
          live(root / manifest.at("runtime_file").get<std::string>())
    {
        if (manifest.at("version") != asset_version || live.data->animations_count != 0)
            throw std::runtime_error("Unsupported asset manifest version or animated runtime GLB");
        body = &live.binding(manifest.at("body_root").get<std::string>());
        port = &live.binding(manifest.at("port_fin_pivot").get<std::string>());
        starboard = &live.binding(manifest.at("starboard_fin_pivot").get<std::string>());
        const auto &joint = manifest.at("fin_joint");
        fin_axis = joint.at("gltf_local_rotation_axis").get<Vec3>();
        if (fin_axis != Vec3{0,0,-1} ||
            std::abs(joint.at("limit").get<double>() - SLX_SB_FIN_LIMIT_RAD) > 1e-8 || body->has_matrix)
            throw std::runtime_error("Unexpected vessel and fin articulation contract");
        auto pivot = [this](const cgltf_node &node, float z) {
            if (node.parent != body || node.has_matrix || std::abs(node.translation[0]) > 1e-5 ||
                std::abs(node.translation[1]+.95f) > 1e-5 || std::abs(node.translation[2]-z) > 1e-5)
                throw std::runtime_error("GLB fin pivot offset or hierarchy differs from manifest");
        };
        pivot(*port, -2.48f);
        pivot(*starboard, 2.48f);
        presentation_available = presentation_files_exist(root, manifest);
        logo = texture(file_bytes(root / "branding/SLX_Official_Logo.png"), false);
        gl::GenVertexArrays(1, &sky_vao);
    }
    ~Impl()
    {
        gl::DeleteTextures(1, &logo);
        gl::DeleteVertexArrays(1, &sky_vao);
    }

    Asset &live_vessel(const View &view)
    {
        if (!std::isfinite(view.pose.roll_rad) || !std::isfinite(view.pose.port_actual_rad) ||
            !std::isfinite(view.pose.starboard_actual_rad) ||
            std::max(std::abs(view.pose.port_actual_rad), std::abs(view.pose.starboard_actual_rad)) >
                SLX_SB_FIN_LIMIT_RAD + 1e-8)
            throw std::runtime_error("Recorded pose exceeds the GLB articulation contract");
        live.rest();
        rotation(*body, {0,1,0}, view.pose.yaw_from_start_rad);
        rotation(*body, {1,0,0}, view.pose.roll_rad);
        rotation(*port, fin_axis, view.pose.port_actual_rad);
        rotation(*starboard, fin_axis, view.pose.starboard_actual_rad);
        return live;
    }

    void load_presentation_ocean()
    {
        if (demo_ocean)
            return;
        const auto ocean = read_json(directory / "ocean_visual_contract.json");
        for (size_t i = 0; i < ocean_omega.size(); ++i)
            ocean_omega[i] = ocean.at("waves").at(i).at("angular_frequency_rad_per_s").get<float>();
        demo_ocean = std::make_unique<Asset>(directory / manifest.at("ocean_visual_file").get<std::string>(), true);
        demo_ocean->binding("SLX_Ocean_Visual");
    }

    Asset *presentation_vessel(const View &view, bool dry)
    {
        if (!presentation_available)
            return nullptr;
        try {
            auto &asset = view.assembly ? component : recorded_clips;
            if (!asset)
                asset = std::make_unique<Asset>(directory /
                    manifest.at(view.assembly ? "component_file" : "animated_file").get<std::string>());
            animate(*asset, view.clip, static_cast<float>(view.clip_s));
            if (!dry && !view.assembly)
                load_presentation_ocean();
            return asset.get();
        } catch (const std::exception &) {
            // Optional presentation material must not interrupt live rendering or replay.
            presentation_available = false;
            return nullptr;
        }
    }

    void draw_water(const View &view, const Mat &vp, Vec3 eye, int width, int height, float storm)
    {
        gl::UseProgram(water.id);
        gl::UniformMatrix4fv(water.at("vp"), 1, GL_FALSE, vp.m);
        gl::Uniform3f(water.at("camera"), eye[0], eye[1], eye[2]);
        gl::Uniform3f(water.at("framebuffer"), static_cast<float>(width), static_cast<float>(height), 0);
        gl::Uniform1f(water.at("storm"), storm);
        gl::Uniform1i(water.at("illustrative"), view.clip != nullptr);
        if (view.clip) {
            std::array<float,6> weights{};
            for (size_t i=0;i<ocean_omega.size();++i) {
                weights[2*i] = static_cast<float>(std::cos(ocean_omega[i] * view.clip_s));
                weights[2*i+1] = static_cast<float>(std::sin(ocean_omega[i] * view.clip_s));
            }
            gl::Uniform1fv(water.at("weight[0]"), 6, weights.data());
            gl::Uniform3f(water.at("at_sample"), static_cast<float>(view.clip_s), 0, 0);
            for (const auto &d : demo_ocean->draws)
                d.geometry->draw();
        } else {
            const auto &s = view.scenario;
            const double omega = 2 * std::numbers::pi / std::max(s.wave_period_s, 2.);
            const double k = omega * omega / 9.80665;
            const double heading = view.pose.size ? view.pose.relative_heading_rad : s.relative_heading_rad;
            const double encounter = omega + k * s.speed_mps * std::cos(heading);
            gl::Uniform3f(water.at("scenario"), static_cast<float>(s.relative_heading_rad), static_cast<float>(k),
                          static_cast<float>(s.wave_height_m));
            gl::Uniform3f(water.at("at_sample"), static_cast<float>(view.pose.sim_ns * 1e-9),
                          static_cast<float>(view.pose.wave_elevation_m), static_cast<float>(encounter));
            grid->draw();
        }
    }

    void draw_ship(Asset &asset, const Mat &vp, Vec3 eye)
    {
        gl::UseProgram(ship.id);
        gl::UniformMatrix4fv(ship_vp, 1, GL_FALSE, vp.m);
        gl::Uniform3f(ship_camera, eye[0], eye[1], eye[2]);
        gl::ActiveTexture(GL_TEXTURE0);
        std::vector<const Draw *> glass;
        auto draw = [&](const Draw &d) {
            if (d.double_sided || d.transparent) gl::Disable(GL_CULL_FACE); else gl::Enable(GL_CULL_FACE);
            gl::UniformMatrix4fv(ship_model, 1, GL_FALSE, d.world.m);
            gl::Uniform4fv(ship_base, 1, d.base.data());
            gl::Uniform3f(ship_surface, d.metallic, d.roughness, d.cutoff);
            gl::Uniform1i(ship_textured, d.image != 0);
            gl::BindTexture(GL_TEXTURE_2D, d.image);
            d.geometry->draw();
        };
        for (auto &d : asset.draws) {
            cgltf_node_transform_world(d.node, d.world.m);
            if (d.transparent) glass.push_back(&d); else draw(d);
        }
        auto distance = [eye](const Draw *d) {
            const Vec3 delta{d->world.m[12]-eye[0], d->world.m[13]-eye[1], d->world.m[14]-eye[2]};
            return dot(delta,delta);
        };
        std::sort(glass.begin(), glass.end(), [&](auto *a, auto *b) { return distance(a) > distance(b); });
        gl::Enable(GL_BLEND);
        gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl::DepthMask(GL_FALSE);
        for (const auto *d : glass)
            draw(*d);
        gl::DepthMask(GL_TRUE);
        gl::Disable(GL_BLEND);
        gl::Disable(GL_CULL_FACE);
    }
};

Renderer::Renderer(const fs::path &assets) : impl_(std::make_unique<Impl>(assets)) {}
Renderer::~Renderer() = default;
unsigned Renderer::logo() const { return impl_->logo; }
bool Renderer::has_presentation() const { return impl_->presentation_available; }

unsigned Renderer::render(unsigned index, int width, int height, const View &requested, Camera camera)
{
    auto &p = *impl_;
    auto &surface = p.surfaces.at(index);
    View view = requested;
    Asset *presentation = view.clip ? p.presentation_vessel(view, camera.dry) : nullptr;
    if (!presentation) {
        view.clip = nullptr;
        view.assembly = false;
    }
    Asset &asset = presentation ? *presentation : p.live_vessel(view);
    width = std::clamp(static_cast<int>(width * 1.35), 4, 2100);
    height = std::clamp(static_cast<int>(height * 1.35), 4, 1200);
    struct Unbind { ~Unbind() { gl::BindFramebuffer(GL_FRAMEBUFFER, 0); } } unbind;
    surface.target(width, height);
    gl::Disable(GL_SCISSOR_TEST);
    gl::Disable(GL_CULL_FACE);
    gl::Disable(GL_BLEND);
    gl::Disable(GL_DEPTH_TEST);
    gl::Viewport(0,0,width,height);
    gl::DepthMask(GL_TRUE);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl::UseProgram(p.sky.id);
    const bool dry = camera.dry || view.assembly;
    const auto sea_height = std::max(view.scenario.wave_height_m, 1.65 * std::abs(view.pose.wave_elevation_m));
    const auto storm = view.clip ? 0.f : static_cast<float>(std::clamp((sea_height - 1.1) / 3.3, 0., 1.));
    gl::Uniform1f(p.sky.at("dry"), dry ? 1.f : 0.f);
    gl::Uniform1f(p.sky.at("storm"), storm);
    gl::BindVertexArray(p.sky_vao);
    gl::DrawArrays(GL_TRIANGLES, 0, 3);
    gl::Enable(GL_DEPTH_TEST);

    const Vec3 target = view.assembly ? Vec3{0,-.28f,.0f} : Vec3{-.40f,1.05f,.0f};
    if (view.assembly)
        camera.distance *= .22f;
    const float aspect = static_cast<float>(width) / height;
    const float radius = camera.distance * std::max(1.f, 1.3f / aspect);
    const float planar = radius * std::cos(camera.elevation);
    const Vec3 eye{target[0]+std::sin(camera.yaw)*planar,
                   target[1]+std::sin(camera.elevation)*radius, target[2]+std::cos(camera.yaw)*planar};
    const auto vp = camera_matrix(eye, target, aspect);
    if (!dry)
        p.draw_water(view, vp, eye, width, height, storm);
    p.draw_ship(asset, vp, eye);
    gl::Disable(GL_DEPTH_TEST);
    return surface.color;
}

void Renderer::screenshot(const fs::path &path, int w, int h)
{
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    std::ofstream out(path, std::ios::binary);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    stbi_flip_vertically_on_write(true);
    auto push = [](void *stream, void *bytes, int size) {
        static_cast<std::ofstream *>(stream)->write(static_cast<const char *>(bytes), size);
    };
    if (!stbi_write_png_to_func(push, &out, w, h, 4, rgba.data(), w*4))
        throw std::runtime_error("Cannot encode the GUI screenshot");
}
} // namespace slx::sb
