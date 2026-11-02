#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include "hellm/Lightmap.h"
#include <limits>
#include <stdexcept>
#include <vector>

namespace helengine::windows {
    /// <summary>Owns seam-expanded original vertices and an independent UV1 stream for one instance submesh.</summary>
    struct Win32SceneLightmapDraw {
        /// <summary>Preserves source position, normal and UV0 in the existing 32-byte renderer format.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> Vertices;
        /// <summary>Stores atlas coordinates in original triangle-corner order.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> Uvs;
        /// <summary>Draws the expanded triangle list without sharing seam vertices.</summary>
        UINT Count=0;
        /// <summary>Creates two independent immutable streams, preserving the original per-corner UV0, normal and position values.</summary>
        static Win32SceneLightmapDraw Create(ID3D11Device* device,const std::vector<float>& vertices,const std::vector<hellm::Vec2>& uvs) {
            if(!device || vertices.empty() || vertices.size()%24!=0 || uvs.size()!=vertices.size()/8 || vertices.size()>std::numeric_limits<UINT>::max()/sizeof(float)) throw std::invalid_argument("Lightmap corner streams must contain corresponding complete triangles.");
            Win32SceneLightmapDraw draw;draw.Count=static_cast<UINT>(uvs.size());
            D3D11_BUFFER_DESC description{};description.Usage=D3D11_USAGE_IMMUTABLE;description.BindFlags=D3D11_BIND_VERTEX_BUFFER;
            description.ByteWidth=static_cast<UINT>(vertices.size()*sizeof(float));D3D11_SUBRESOURCE_DATA data{vertices.data(),0,0};
            if(FAILED(device->CreateBuffer(&description,&data,&draw.Vertices))) throw std::runtime_error("Create lightmap original corner stream failed.");
            description.ByteWidth=static_cast<UINT>(uvs.size()*sizeof(hellm::Vec2));data.pSysMem=uvs.data();
            if(FAILED(device->CreateBuffer(&description,&data,&draw.Uvs))) throw std::runtime_error("Create lightmap UV1 corner stream failed.");
            return draw;
        }
        /// <summary>Binds expanded positions, original normals and UV0 alongside the instance-specific atlas coordinates.</summary>
        void Bind(ID3D11DeviceContext* context) const {
            ID3D11Buffer* streams[]={Vertices.Get(),Uvs.Get()};const UINT strides[]={32,8},offsets[]={0,0};context->IASetVertexBuffers(0,2,streams,strides,offsets);
        }
    };
}
