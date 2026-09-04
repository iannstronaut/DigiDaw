#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <array>
#include <iostream>

namespace digidaw::adapters::gui {

struct ShaderAudioParams {
    float u_time{0.0f};
    float u_width{1200.0f};
    float u_height{800.0f};
    float u_master_peak{0.0f};

    float u_track_peaks[4]{0.0f, 0.0f, 0.0f, 0.0f};

    float u_is_playing{0.0f};
    float u_spm_pos{0.0f};
    float u_padding[2]{0.0f, 0.0f};
};

class D3DShaderVisualizer {
public:
    D3DShaderVisualizer() = default;
    ~D3DShaderVisualizer() { release(); }

    D3DShaderVisualizer(const D3DShaderVisualizer&) = delete;
    D3DShaderVisualizer& operator=(const D3DShaderVisualizer&) = delete;

    bool init(ID3D11Device* device) {
        if (!device) return false;
        release();

        // 1. Compile Vertex Shader (Procedural Fullscreen Triangle)
        const char* vs_code =
            "struct VS_OUT {\n"
            "    float4 pos : SV_POSITION;\n"
            "    float2 uv  : TEXCOORD0;\n"
            "};\n"
            "VS_OUT VSMain(uint id : SV_VertexID) {\n"
            "    VS_OUT o;\n"
            "    float2 uv = float2((id << 1) & 2, id & 2);\n"
            "    o.uv = uv;\n"
            "    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\n"
            "    return o;\n"
            "}\n";

        ID3DBlob* vs_blob = nullptr;
        ID3DBlob* err_blob = nullptr;
        HRESULT hr = D3DCompile(vs_code, strlen(vs_code), nullptr, nullptr, nullptr,
                                "VSMain", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                &vs_blob, &err_blob);
        if (FAILED(hr) || !vs_blob) {
            if (err_blob) {
                std::cerr << "[D3D Shader] VS compile error: " << (char*)err_blob->GetBufferPointer() << "\n";
                err_blob->Release();
            }
            return false;
        }

        hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vertex_shader_);
        vs_blob->Release();
        if (FAILED(hr)) return false;

        // 2. Compile Pixel Shader (Audio-Reactive Waveform, Laser Spectrum & Ambient Glow)
        const char* ps_code =
            "cbuffer AudioBuffer : register(b0) {\n"
            "    float u_time;\n"
            "    float u_width;\n"
            "    float u_height;\n"
            "    float u_master_peak;\n"
            "    float4 u_track_peaks;\n"
            "    float u_is_playing;\n"
            "    float u_spm_pos;\n"
            "    float2 u_padding;\n"
            "};\n"
            "struct VS_OUT {\n"
            "    float4 pos : SV_POSITION;\n"
            "    float2 uv  : TEXCOORD0;\n"
            "};\n"
            "float4 PSMain(VS_OUT input) : SV_TARGET {\n"
            "    float2 uv = input.uv;\n"
            "    // Near-black dark surface (#090a0c - DESIGN.md --bg-app)\n"
            "    float3 bg = float3(0.0353, 0.0392, 0.0471);\n"
            "\n"
            "    // Subtle ambient reactive vignette at bottom (electric violet signature tint)\n"
            "    if (uv.y > 0.7) {\n"
            "        float bot_norm = (uv.y - 0.7) / 0.3;\n"
            "        float aura = bot_norm * u_master_peak * 0.035;\n"
            "        bg += float3(0.486, 0.227, 0.929) * aura;\n"
            "    }\n"
            "\n"
            "    return float4(bg, 1.0);\n"
            "}\n";

        ID3DBlob* ps_blob = nullptr;
        hr = D3DCompile(ps_code, strlen(ps_code), nullptr, nullptr, nullptr,
                        "PSMain", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                        &ps_blob, &err_blob);
        if (FAILED(hr) || !ps_blob) {
            if (err_blob) {
                std::cerr << "[D3D Shader] PS compile error: " << (char*)err_blob->GetBufferPointer() << "\n";
                err_blob->Release();
            }
            return false;
        }

        hr = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &pixel_shader_);
        ps_blob->Release();
        if (FAILED(hr)) return false;

        // 3. Create Constant Buffer (48 bytes, 16-byte aligned)
        D3D11_BUFFER_DESC cb_desc = {};
        cb_desc.ByteWidth = sizeof(ShaderAudioParams);
        cb_desc.Usage = D3D11_USAGE_DYNAMIC;
        cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        hr = device->CreateBuffer(&cb_desc, nullptr, &constant_buffer_);
        if (FAILED(hr)) return false;

        is_initialized_ = true;
        return true;
    }

    void render(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv,
                int width, int height, float time_sec,
                float master_peak, const std::array<float, 8>& track_peaks,
                bool is_playing, float spm_norm) {
        if (!is_initialized_ || !ctx || !rtv) return;

        // 1. Update Constant Buffer
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = ctx->Map(constant_buffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            ShaderAudioParams* params = reinterpret_cast<ShaderAudioParams*>(mapped.pData);
            params->u_time = time_sec;
            params->u_width = static_cast<float>(width);
            params->u_height = static_cast<float>(height);
            params->u_master_peak = master_peak;
            params->u_track_peaks[0] = track_peaks[1]; // Track 1
            params->u_track_peaks[1] = track_peaks[2]; // Track 2
            params->u_track_peaks[2] = track_peaks[3]; // Track 3
            params->u_track_peaks[3] = track_peaks[4]; // Track 4
            params->u_is_playing = is_playing ? 1.0f : 0.0f;
            params->u_spm_pos = spm_norm;
            ctx->Unmap(constant_buffer_, 0);
        }

        // 2. Setup Viewport and Pipeline
        D3D11_VIEWPORT vp = {};
        vp.Width = static_cast<float>(width);
        vp.Height = static_cast<float>(height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0;
        vp.TopLeftY = 0;

        ctx->RSSetViewports(1, &vp);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);

        // 3. Bind Shaders and Constant Buffer
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->IASetInputLayout(nullptr);
        ctx->VSSetShader(vertex_shader_, nullptr, 0);
        ctx->PSSetShader(pixel_shader_, nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, &constant_buffer_);

        // 4. Draw Fullscreen Triangle (3 vertices generated by SV_VertexID)
        ctx->Draw(3, 0);

        // 5. Unbind render target to allow Direct2D to bind the surface cleanly
        ID3D11RenderTargetView* null_rtv[1] = {nullptr};
        ctx->OMSetRenderTargets(1, null_rtv, nullptr);
    }

    void release() {
        if (constant_buffer_) { constant_buffer_->Release(); constant_buffer_ = nullptr; }
        if (pixel_shader_) { pixel_shader_->Release(); pixel_shader_ = nullptr; }
        if (vertex_shader_) { vertex_shader_->Release(); vertex_shader_ = nullptr; }
        is_initialized_ = false;
    }

    [[nodiscard]] bool is_initialized() const noexcept { return is_initialized_; }

private:
    ID3D11VertexShader* vertex_shader_{nullptr};
    ID3D11PixelShader* pixel_shader_{nullptr};
    ID3D11Buffer* constant_buffer_{nullptr};
    bool is_initialized_{false};
};

} // namespace digidaw::adapters::gui
