/*
 * Copyright (C) 2014 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/reshade#license
 */

#include "dll_log.hpp"
#include "d3d11_device.hpp"
#include "d3d11_device_context.hpp"
#include "d3d11_command_list.hpp"
#include "reshade_api_type_utils.hpp"

D3D11DeviceContext::D3D11DeviceContext(D3D11Device *device, ID3D11DeviceContext  *original) :
	device_context_impl(device, original),
	_interface_version(0),
	_device(device)
{
	assert(_orig != nullptr && _device != nullptr);
}
D3D11DeviceContext::D3D11DeviceContext(D3D11Device *device, ID3D11DeviceContext1 *original) :
	device_context_impl(device, original),
	_interface_version(1),
	_device(device)
{
	assert(_orig != nullptr && _device != nullptr);
}
D3D11DeviceContext::D3D11DeviceContext(D3D11Device *device, ID3D11DeviceContext2 *original) :
	device_context_impl(device, original),
	_interface_version(2),
	_device(device)
{
	assert(_orig != nullptr && _device != nullptr);
}
D3D11DeviceContext::D3D11DeviceContext(D3D11Device *device, ID3D11DeviceContext3 *original) :
	device_context_impl(device, original),
	_interface_version(3),
	_device(device)
{
	assert(_orig != nullptr && _device != nullptr);
}

bool D3D11DeviceContext::check_and_upgrade_interface(REFIID riid)
{
	if (riid == __uuidof(this) ||
		riid == __uuidof(IUnknown) ||
		riid == __uuidof(ID3D11DeviceChild))
		return true;

	static const IID iid_lookup[] = {
		__uuidof(ID3D11DeviceContext),
		__uuidof(ID3D11DeviceContext1),
		__uuidof(ID3D11DeviceContext2),
		__uuidof(ID3D11DeviceContext3),
		__uuidof(ID3D11DeviceContext4),
	};

	for (unsigned int version = 0; version < ARRAYSIZE(iid_lookup); ++version)
	{
		if (riid != iid_lookup[version])
			continue;

		if (version > _interface_version)
		{
			IUnknown *new_interface = nullptr;
			if (FAILED(_orig->QueryInterface(riid, reinterpret_cast<void **>(&new_interface))))
				return false;
#if RESHADE_VERBOSE_LOG
			LOG(DEBUG) << "Upgraded ID3D11DeviceContext" << _interface_version << " object " << this << " to ID3D11DeviceContext" << version << '.';
#endif
			_orig->Release();
			_orig = static_cast<ID3D11DeviceContext *>(new_interface);
			_interface_version = version;
		}

		return true;
	}

	return false;
}

#if RESHADE_ADDON
void D3D11DeviceContext::invoke_bind_render_targets_event(UINT count, ID3D11RenderTargetView *const *targets, ID3D11DepthStencilView *dsv)
{
	assert(count <= D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT);

	if ((count == 0 && dsv == nullptr) ||
		reshade::addon::event_list[static_cast<uint32_t>(reshade::addon_event::begin_render_pass)].empty())
		return;

#ifndef WIN64
	reshade::api::resource_view rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
	for (UINT i = 0; i < count; ++i)
		rtvs[i] = { reinterpret_cast<uintptr_t>(targets[i]) };
#else
	static_assert(sizeof(*targets) == sizeof(reshade::api::resource_view));
	const auto rtvs = reinterpret_cast<const reshade::api::resource_view *>(targets);
#endif

	_has_open_render_pass = true;
	reshade::invoke_addon_event<reshade::addon_event::begin_render_pass>(this, count, rtvs, reshade::api::resource_view { reinterpret_cast<uintptr_t>(dsv) });
}
void D3D11DeviceContext::invoke_bind_vertex_buffers_event(UINT first, UINT count, ID3D11Buffer *const *buffers, const UINT *strides, const UINT *offsets)
{
	assert(count <= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT);

	if (reshade::addon::event_list[static_cast<uint32_t>(reshade::addon_event::bind_vertex_buffers)].empty())
		return;

#ifndef WIN64
	reshade::api::resource buffer_handles[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
	for (UINT i = 0; i < count; ++i)
		buffer_handles[i] = { reinterpret_cast<uintptr_t>(buffers[i]) };
#else
	static_assert(sizeof(*buffers) == sizeof(reshade::api::resource));
	const auto buffer_handles = reinterpret_cast<const reshade::api::resource *>(buffers);
#endif

	uint64_t offsets_64[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
	for (UINT i = 0; i < count; ++i)
		offsets_64[i] = offsets[i];

	reshade::invoke_addon_event<reshade::addon_event::bind_vertex_buffers>(this, first, count, buffer_handles, offsets_64, strides);
}
void D3D11DeviceContext::invoke_bind_samplers_event(reshade::api::shader_stage stage, UINT first, UINT count, ID3D11SamplerState *const *objects)
{
	assert(count <= D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT);

	if (reshade::addon::event_list[static_cast<uint32_t>(reshade::addon_event::push_descriptors)].empty())
		return;

#ifndef WIN64
	reshade::api::sampler descriptors[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
	for (UINT i = 0; i < count; ++i)
		descriptors[i] = { reinterpret_cast<uintptr_t>(objects[i]) };
#else
	static_assert(sizeof(*objects) == sizeof(reshade::api::sampler));
	const auto descriptors = reinterpret_cast<const reshade::api::sampler *>(objects);
#endif

	reshade::invoke_addon_event<reshade::addon_event::push_descriptors>(this, stage, reshade::api::pipeline_layout { 0 }, 0, reshade::api::descriptor_type::sampler, first, count, descriptors);
}
void D3D11DeviceContext::invoke_bind_shader_resource_views_event(reshade::api::shader_stage stage, UINT first, UINT count, ID3D11ShaderResourceView *const *objects)
{
	assert(count <= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);

	if (reshade::addon::event_list[static_cast<uint32_t>(reshade::addon_event::push_descriptors)].empty())
		return;

#ifndef WIN64
	reshade::api::resource_view descriptors[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
	for (UINT i = 0; i < count; ++i)
		descriptors[i] = { reinterpret_cast<uintptr_t>(objects[i]) };
#else
	static_assert(sizeof(*objects) == sizeof(reshade::api::resource_view));
	const auto descriptors = reinterpret_cast<const reshade::api::resource_view *>(objects);
#endif

	reshade::invoke_addon_event<reshade::addon_event::push_descriptors>(this, stage, reshade::api::pipeline_layout { 0 }, 0, reshade::api::descriptor_type::shader_resource_view, first, count, descriptors);
}
void D3D11DeviceContext::invoke_bind_unordered_access_views_event(reshade::api::shader_stage stage, UINT first, UINT count, ID3D11UnorderedAccessView *const *objects)
{
	assert(count <= D3D11_1_UAV_SLOT_COUNT);

	if (reshade::addon::event_list[static_cast<uint32_t>(reshade::addon_event::push_descriptors)].empty())
		return;

#ifndef WIN64
	reshade::api::resource_view descriptors[D3D11_1_UAV_SLOT_COUNT];
	for (UINT i = 0; i < count; ++i)
		descriptors[i] = { reinterpret_cast<uintptr_t>(objects[i]) };
#else
	static_assert(sizeof(*objects) == sizeof(reshade::api::resource_view));
	const auto descriptors = reinterpret_cast<const reshade::api::resource_view *>(objects);
#endif

	reshade::invoke_addon_event<reshade::addon_event::push_descriptors>(this, stage, reshade::api::pipeline_layout { 0 }, 0, reshade::api::descriptor_type::unordered_access_view, first, count, descriptors);
}
void D3D11DeviceContext::invoke_bind_constant_buffers_event(reshade::api::shader_stage stage, UINT first, UINT count, ID3D11Buffer *const *objects)
{
	assert(count <= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);

	if (reshade::addon::event_list[static_cast<uint32_t>(reshade::addon_event::push_descriptors)].empty())
		return;

#ifndef WIN64
	reshade::api::resource descriptors[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
	for (UINT i = 0; i < count; ++i)
		descriptors[i] = { reinterpret_cast<uintptr_t>(objects[i]) };
#else
	static_assert(sizeof(*objects) == sizeof(reshade::api::resource));
	const auto descriptors = reinterpret_cast<const reshade::api::resource *>(objects);
#endif

	reshade::invoke_addon_event<reshade::addon_event::push_descriptors>(this, stage, reshade::api::pipeline_layout { 0 }, 0, reshade::api::descriptor_type::constant_buffer, first, count, descriptors);
}
#endif

HRESULT STDMETHODCALLTYPE D3D11DeviceContext::QueryInterface(REFIID riid, void **ppvObj)
{
	if (ppvObj == nullptr)
		return E_POINTER;

	if (check_and_upgrade_interface(riid))
	{
		AddRef();
		*ppvObj = this;
		return S_OK;
	}

	return _orig->QueryInterface(riid, ppvObj);
}
ULONG   STDMETHODCALLTYPE D3D11DeviceContext::AddRef()
{
	_orig->AddRef();
	return InterlockedIncrement(&_ref);
}
ULONG   STDMETHODCALLTYPE D3D11DeviceContext::Release()
{
	const ULONG ref = InterlockedDecrement(&_ref);
	if (ref != 0)
		return _orig->Release(), ref;

	const auto orig = _orig;
	const auto interface_version = _interface_version;
#if RESHADE_VERBOSE_LOG
	LOG(DEBUG) << "Destroying " << "ID3D11DeviceContext" << interface_version << " object " << this << " (" << orig << ").";
#endif
	delete this;

	const ULONG ref_orig = orig->Release();
	if (ref_orig != 0) // Verify internal reference count
		LOG(WARN) << "Reference count for " << "ID3D11DeviceContext" << interface_version << " object " << this << " (" << orig << ") is inconsistent (" << ref_orig << ").";
	return 0;
}

void    STDMETHODCALLTYPE D3D11DeviceContext::GetDevice(ID3D11Device **ppDevice)
{
	if (ppDevice == nullptr)
		return;

	_device->AddRef();
	*ppDevice = _device;
}
HRESULT STDMETHODCALLTYPE D3D11DeviceContext::GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData)
{
	return _orig->GetPrivateData(guid, pDataSize, pData);
}
HRESULT STDMETHODCALLTYPE D3D11DeviceContext::SetPrivateData(REFGUID guid, UINT DataSize, const void *pData)
{
	return _orig->SetPrivateData(guid, DataSize, pData);
}
HRESULT STDMETHODCALLTYPE D3D11DeviceContext::SetPrivateDataInterface(REFGUID guid, const IUnknown *pData)
{
	return _orig->SetPrivateDataInterface(guid, pData);
}

void    STDMETHODCALLTYPE D3D11DeviceContext::VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
	_orig->VSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
#if RESHADE_ADDON
	invoke_bind_constant_buffers_event(reshade::api::shader_stage::vertex, StartSlot, NumBuffers, ppConstantBuffers);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
	_orig->PSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
#if RESHADE_ADDON
	invoke_bind_shader_resource_views_event(reshade::api::shader_stage::pixel, StartSlot, NumViews, ppShaderResourceViews);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::PSSetShader(ID3D11PixelShader *pPixelShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
	_orig->PSSetShader(pPixelShader, ppClassInstances, NumClassInstances);
#if RESHADE_ADDON
	reshade::invoke_addon_event<reshade::addon_event::bind_pipeline>(this, reshade::api::pipeline_type::graphics_pixel_shader, reshade::api::pipeline { reinterpret_cast<uintptr_t>(pPixelShader) });
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
	_orig->PSSetSamplers(StartSlot, NumSamplers, ppSamplers);
#if RESHADE_ADDON
	invoke_bind_samplers_event(reshade::api::shader_stage::pixel, StartSlot, NumSamplers, ppSamplers);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::VSSetShader(ID3D11VertexShader *pVertexShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
	_orig->VSSetShader(pVertexShader, ppClassInstances, NumClassInstances);
#if RESHADE_ADDON
	reshade::invoke_addon_event<reshade::addon_event::bind_pipeline>(this, reshade::api::pipeline_type::graphics_vertex_shader, reshade::api::pipeline { reinterpret_cast<uintptr_t>(pVertexShader) });
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
#if RESHADE_ADDON
	if (reshade::invoke_addon_event<reshade::addon_event::draw_indexed>(this, IndexCount, 1, StartIndexLocation, BaseVertexLocation, 0))
		return;
#endif
	_orig->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::Draw(UINT VertexCount, UINT StartVertexLocation)
{
#if RESHADE_ADDON
	if (reshade::invoke_addon_event<reshade::addon_event::draw>(this, VertexCount, 1, StartVertexLocation, 0))
		return;
#endif
	_orig->Draw(VertexCount, StartVertexLocation);
}
HRESULT STDMETHODCALLTYPE D3D11DeviceContext::Map(ID3D11Resource *pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE *pMappedResource)
{
	return _orig->Map(pResource, Subresource, MapType, MapFlags, pMappedResource);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::Unmap(ID3D11Resource *pResource, UINT Subresource)
{
	_orig->Unmap(pResource, Subresource);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
	_orig->PSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
#if RESHADE_ADDON
	invoke_bind_constant_buffers_event(reshade::api::shader_stage::pixel, StartSlot, NumBuffers, ppConstantBuffers);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::IASetInputLayout(ID3D11InputLayout *pInputLayout)
{
	_orig->IASetInputLayout(pInputLayout);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppVertexBuffers, const UINT *pStrides, const UINT *pOffsets)
{
	_orig->IASetVertexBuffers(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);
#if RESHADE_ADDON
	invoke_bind_vertex_buffers_event(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::IASetIndexBuffer(ID3D11Buffer *pIndexBuffer, DXGI_FORMAT Format, UINT Offset)
{
	_orig->IASetIndexBuffer(pIndexBuffer, Format, Offset);
#if RESHADE_ADDON
	reshade::invoke_addon_event<reshade::addon_event::bind_index_buffer>(this, reshade::api::resource { reinterpret_cast<uintptr_t>(pIndexBuffer) }, Offset, pIndexBuffer == nullptr ? 0 : Format == DXGI_FORMAT_R16_UINT ? 2 : 4);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
{
#if RESHADE_ADDON
	if (reshade::invoke_addon_event<reshade::addon_event::draw_indexed>(this, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation))
		return;
#endif
	_orig->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation)
{
#if RESHADE_ADDON
	if (reshade::invoke_addon_event<reshade::addon_event::draw>(this, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation))
		return;
#endif
	_orig->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::GSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
	_orig->GSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
#if RESHADE_ADDON
	invoke_bind_constant_buffers_event(reshade::api::shader_stage::geometry, StartSlot, NumBuffers, ppConstantBuffers);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::GSSetShader(ID3D11GeometryShader *pShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
	_orig->GSSetShader(pShader, ppClassInstances, NumClassInstances);
#if RESHADE_ADDON
	reshade::invoke_addon_event<reshade::addon_event::bind_pipeline>(this, reshade::api::pipeline_type::graphics_geometry_shader, reshade::api::pipeline { reinterpret_cast<uintptr_t>(pShader) });
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology)
{
	_orig->IASetPrimitiveTopology(Topology);

#if RESHADE_ADDON
	const reshade::api::pipeline_state state = reshade::api::pipeline_state::primitive_topology;
	reshade::invoke_addon_event<reshade::addon_event::bind_pipeline_states>(this, 1, &state, reinterpret_cast<const uint32_t *>(&Topology));
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
	_orig->VSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
#if RESHADE_ADDON
	invoke_bind_shader_resource_views_event(reshade::api::shader_stage::vertex, StartSlot, NumViews, ppShaderResourceViews);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
	_orig->VSSetSamplers(StartSlot, NumSamplers, ppSamplers);
#if RESHADE_ADDON
	invoke_bind_samplers_event(reshade::api::shader_stage::vertex, StartSlot, NumSamplers, ppSamplers);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::Begin(ID3D11Asynchronous *pAsync)
{
	_orig->Begin(pAsync);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::End(ID3D11Asynchronous *pAsync)
{
	_orig->End(pAsync);
}
HRESULT STDMETHODCALLTYPE D3D11DeviceContext::GetData(ID3D11Asynchronous *pAsync, void *pData, UINT DataSize, UINT GetDataFlags)
{
	return _orig->GetData(pAsync, pData, DataSize, GetDataFlags);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::SetPredication(ID3D11Predicate *pPredicate, BOOL PredicateValue)
{
	_orig->SetPredication(pPredicate, PredicateValue);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::GSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
	_orig->GSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
#if RESHADE_ADDON
	invoke_bind_shader_resource_views_event(reshade::api::shader_stage::geometry, StartSlot, NumViews, ppShaderResourceViews);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::GSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
	_orig->GSSetSamplers(StartSlot, NumSamplers, ppSamplers);
#if RESHADE_ADDON
	invoke_bind_samplers_event(reshade::api::shader_stage::geometry, StartSlot, NumSamplers, ppSamplers);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView)
{
#if RESHADE_ADDON
	if (_has_open_render_pass)
	{
		reshade::invoke_addon_event<reshade::addon_event::finish_render_pass>(this);
		_has_open_render_pass = false;
	}
#endif

	_orig->OMSetRenderTargets(NumViews, ppRenderTargetViews, pDepthStencilView);

#if RESHADE_ADDON
	invoke_bind_render_targets_event(NumViews, ppRenderTargetViews, pDepthStencilView);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts)
{
#if RESHADE_ADDON
	if (_has_open_render_pass)
	{
		reshade::invoke_addon_event<reshade::addon_event::finish_render_pass>(this);
		_has_open_render_pass = false;
	}
#endif

	_orig->OMSetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRenderTargetViews, pDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);

#if RESHADE_ADDON
	invoke_bind_render_targets_event(NumRTVs, ppRenderTargetViews, pDepthStencilView);
	invoke_bind_unordered_access_views_event(reshade::api::shader_stage::pixel, UAVStartSlot, NumUAVs, ppUnorderedAccessViews);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::OMSetBlendState(ID3D11BlendState *pBlendState, const FLOAT BlendFactor[4], UINT SampleMask)
{
	_orig->OMSetBlendState(pBlendState, BlendFactor, SampleMask);

#if RESHADE_ADDON
	reshade::invoke_addon_event<reshade::addon_event::bind_pipeline>(this,
		reshade::api::pipeline_type::graphics_blend_state, reshade::api::pipeline { reinterpret_cast<uintptr_t>(pBlendState) });

	const reshade::api::pipeline_state states[2] = { reshade::api::pipeline_state::blend_constant, reshade::api::pipeline_state::sample_mask };
	const uint32_t values[2] = {
		(BlendFactor == nullptr) ? 0xFFFFFFFF : // Default blend factor is { 1, 1, 1, 1 }
			((static_cast<uint32_t>(BlendFactor[0] * 255.f) & 0xFF)) |
			((static_cast<uint32_t>(BlendFactor[1] * 255.f) & 0xFF) << 8) |
			((static_cast<uint32_t>(BlendFactor[2] * 255.f) & 0xFF) << 16) |
			((static_cast<uint32_t>(BlendFactor[3] * 255.f) & 0xFF) << 24), SampleMask };
	reshade::invoke_addon_event<reshade::addon_event::bind_pipeline_states>(this, 2, states, values);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::OMSetDepthStencilState(ID3D11DepthStencilState *pDepthStencilState, UINT StencilRef)
{
	_orig->OMSetDepthStencilState(pDepthStencilState, StencilRef);

#if RESHADE_ADDON
	reshade::invoke_addon_event<reshade::addon_event::bind_pipeline>(this,
		reshade::api::pipeline_type::graphics_depth_stencil_state, reshade::api::pipeline { reinterpret_cast<uintptr_t>(pDepthStencilState) });

	const reshade::api::pipeline_state state = reshade::api::pipeline_state::stencil_reference_value;
	reshade::invoke_addon_event<reshade::addon_event::bind_pipeline_states>(this, 1, &state, &StencilRef);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::SOSetTargets(UINT NumBuffers, ID3D11Buffer *const *ppSOTargets, const UINT *pOffsets)
{
	_orig->SOSetTargets(NumBuffers, ppSOTargets, pOffsets);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DrawAuto()
{
#if RESHADE_ADDON
	if (reshade::invoke_addon_event<reshade::addon_event::draw>(this, 0, 0, 0, 0))
		return;
#endif
	_orig->DrawAuto();
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DrawIndexedInstancedIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
#if RESHADE_ADDON
	if (reshade::invoke_addon_event<reshade::addon_event::draw_or_dispatch_indirect>(this, 2, reshade::api::resource { reinterpret_cast<uintptr_t>(pBufferForArgs) }, AlignedByteOffsetForArgs, 1, 0))
		return;
#endif
	_orig->DrawIndexedInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DrawInstancedIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
#if RESHADE_ADDON
	if (reshade::invoke_addon_event<reshade::addon_event::draw_or_dispatch_indirect>(this, 1, reshade::api::resource { reinterpret_cast<uintptr_t>(pBufferForArgs) }, AlignedByteOffsetForArgs, 1, 0))
		return;
#endif
	_orig->DrawInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ)
{
#if RESHADE_ADDON
	if (reshade::invoke_addon_event<reshade::addon_event::dispatch>(this, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ))
		return;
#endif
	_orig->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DispatchIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs)
{
#if RESHADE_ADDON
	if (reshade::invoke_addon_event<reshade::addon_event::draw_or_dispatch_indirect>(this, 3, reshade::api::resource { reinterpret_cast<uintptr_t>(pBufferForArgs) }, AlignedByteOffsetForArgs, 1, 0))
		return;
#endif
	_orig->DispatchIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::RSSetState(ID3D11RasterizerState *pRasterizerState)
{
	_orig->RSSetState(pRasterizerState);

#if RESHADE_ADDON
	reshade::invoke_addon_event<reshade::addon_event::bind_pipeline>(this,
		reshade::api::pipeline_type::graphics_rasterizer_state, reshade::api::pipeline { reinterpret_cast<uintptr_t>(pRasterizerState) });
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT *pViewports)
{
	_orig->RSSetViewports(NumViewports, pViewports);

#if RESHADE_ADDON
	static_assert(sizeof(D3D11_VIEWPORT) == (sizeof(float) * 6));

	reshade::invoke_addon_event<reshade::addon_event::bind_viewports>(this, 0, NumViewports, reinterpret_cast<const float *>(pViewports));
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::RSSetScissorRects(UINT NumRects, const D3D11_RECT *pRects)
{
	_orig->RSSetScissorRects(NumRects, pRects);

#if RESHADE_ADDON
	static_assert(sizeof(D3D11_RECT) == (sizeof(int32_t) * 4));

	reshade::invoke_addon_event<reshade::addon_event::bind_scissor_rects>(this, 0, NumRects, reinterpret_cast<const int32_t *>(pRects));
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CopySubresourceRegion(ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox)
{
#if RESHADE_ADDON
	assert(pDstResource != nullptr && pSrcResource != nullptr);

	D3D11_RESOURCE_DIMENSION type;
	pDstResource->GetType(&type);
	if (type == D3D11_RESOURCE_DIMENSION_BUFFER)
	{
		assert(SrcSubresource == 0 && DstSubresource == 0);

		if (reshade::invoke_addon_event<reshade::addon_event::copy_buffer_region>(this,
			reshade::api::resource { reinterpret_cast<uintptr_t>(pSrcResource) }, pSrcBox != nullptr ? pSrcBox->left : 0,
			reshade::api::resource { reinterpret_cast<uintptr_t>(pDstResource) }, DstX, pSrcBox != nullptr ? pSrcBox->right - pSrcBox->left : ~0ull))
			return;
	}
	else
	{
		int32_t dst_box[6] = { static_cast<int32_t>(DstX), static_cast<int32_t>(DstY), static_cast<int32_t>(DstZ) };
		if (pSrcBox != nullptr)
		{
			dst_box[3] = dst_box[0] + pSrcBox->right - pSrcBox->left;
			dst_box[4] = dst_box[1] + pSrcBox->bottom - pSrcBox->top;
			dst_box[5] = dst_box[2] + pSrcBox->back - pSrcBox->front;
		}
		else
		{
			assert(DstX == 0 && DstY == 0 && DstZ == 0);
		}

		static_assert(sizeof(D3D11_BOX) == (sizeof(int32_t) * 6));

		if (reshade::invoke_addon_event<reshade::addon_event::copy_texture_region>(this,
			reshade::api::resource { reinterpret_cast<uintptr_t>(pSrcResource) }, SrcSubresource, reinterpret_cast<const int32_t *>(pSrcBox),
			reshade::api::resource { reinterpret_cast<uintptr_t>(pDstResource) }, DstSubresource, DstX != 0 || DstY != 0 || DstZ != 0 ? dst_box : nullptr, reshade::api::texture_filter::min_mag_mip_point))
			return;
	}
#endif
	_orig->CopySubresourceRegion(pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CopyResource(ID3D11Resource *pDstResource, ID3D11Resource *pSrcResource)
{
#if RESHADE_ADDON
	if (reshade::invoke_addon_event<reshade::addon_event::copy_resource>(this, reshade::api::resource { reinterpret_cast<uintptr_t>(pSrcResource) }, reshade::api::resource { reinterpret_cast<uintptr_t>(pDstResource) }))
		return;
#endif
	_orig->CopyResource(pDstResource, pSrcResource);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::UpdateSubresource(ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
#if RESHADE_ADDON
	assert(pDstResource != nullptr);

	D3D11_RESOURCE_DIMENSION type;
	pDstResource->GetType(&type);
	if (type == D3D11_RESOURCE_DIMENSION_BUFFER)
	{
		assert(DstSubresource == 0);

		if (reshade::invoke_addon_event<reshade::addon_event::upload_buffer_region>(_device,
			pSrcData,
			reshade::api::resource { reinterpret_cast<uintptr_t>(pDstResource) },
			pDstBox != nullptr ? pDstBox->left : 0,
			pDstBox != nullptr ? pDstBox->right - pDstBox->left : SrcRowPitch))
			return;
	}
	else
	{
		static_assert(sizeof(D3D11_BOX) == (sizeof(int32_t) * 6));

		if (reshade::invoke_addon_event<reshade::addon_event::upload_texture_region>(_device,
			reshade::api::subresource_data { pSrcData, SrcRowPitch, SrcDepthPitch },
			reshade::api::resource { reinterpret_cast<uintptr_t>(pDstResource) }, DstSubresource, reinterpret_cast<const int32_t *>(pDstBox)))
			return;
	}
#endif
	_orig->UpdateSubresource(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CopyStructureCount(ID3D11Buffer *pDstBuffer, UINT DstAlignedByteOffset, ID3D11UnorderedAccessView *pSrcView)
{
#if RESHADE_ADDON
	assert(pSrcView != nullptr);

	com_ptr<ID3D11Resource> src;
	pSrcView->GetResource(&src);
	D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
	pSrcView->GetDesc(&desc);

	if (desc.ViewDimension == D3D11_UAV_DIMENSION_BUFFER &&
		reshade::invoke_addon_event<reshade::addon_event::copy_buffer_region>(this,
		reshade::api::resource { reinterpret_cast<uintptr_t>(src.get()) }, desc.Buffer.FirstElement,
		reshade::api::resource { reinterpret_cast<uintptr_t>(pDstBuffer) }, DstAlignedByteOffset, desc.Buffer.NumElements))
		return;
#endif
	_orig->CopyStructureCount(pDstBuffer, DstAlignedByteOffset, pSrcView);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::ClearRenderTargetView(ID3D11RenderTargetView *pRenderTargetView, const FLOAT ColorRGBA[4])
{
#if RESHADE_ADDON
	if (const reshade::api::resource_view rtv = { reinterpret_cast<uintptr_t>(pRenderTargetView) };
		reshade::invoke_addon_event<reshade::addon_event::clear_render_target_views>(this, 1, &rtv, ColorRGBA))
		return;
#endif
	_orig->ClearRenderTargetView(pRenderTargetView, ColorRGBA);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView *pUnorderedAccessView, const UINT Values[4])
{
#if RESHADE_ADDON
	if (reshade::invoke_addon_event<reshade::addon_event::clear_unordered_access_view_uint>(this, reshade::api::resource_view { reinterpret_cast<uintptr_t>(pUnorderedAccessView) }, Values))
		return;
#endif
	_orig->ClearUnorderedAccessViewUint(pUnorderedAccessView, Values);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView *pUnorderedAccessView, const FLOAT Values[4])
{
#if RESHADE_ADDON
	if (reshade::invoke_addon_event<reshade::addon_event::clear_unordered_access_view_float>(this, reshade::api::resource_view { reinterpret_cast<uintptr_t>(pUnorderedAccessView) }, Values))
		return;
#endif
	_orig->ClearUnorderedAccessViewFloat(pUnorderedAccessView, Values);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::ClearDepthStencilView(ID3D11DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
{
#if RESHADE_ADDON
	if (reshade::invoke_addon_event<reshade::addon_event::clear_depth_stencil_view>(this, reshade::api::resource_view { reinterpret_cast<uintptr_t>(pDepthStencilView) }, ClearFlags, Depth, Stencil))
		return;
#endif
	_orig->ClearDepthStencilView(pDepthStencilView, ClearFlags, Depth, Stencil);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::GenerateMips(ID3D11ShaderResourceView *pShaderResourceView)
{
	_orig->GenerateMips(pShaderResourceView);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::SetResourceMinLOD(ID3D11Resource *pResource, FLOAT MinLOD)
{
	_orig->SetResourceMinLOD(pResource, MinLOD);
}
FLOAT   STDMETHODCALLTYPE D3D11DeviceContext::GetResourceMinLOD(ID3D11Resource *pResource)
{
	return _orig->GetResourceMinLOD(pResource);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::ResolveSubresource(ID3D11Resource *pDstResource, UINT DstSubresource, ID3D11Resource *pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format)
{
#if RESHADE_ADDON
	if (reshade::invoke_addon_event<reshade::addon_event::resolve_texture_region>(this,
		reshade::api::resource { reinterpret_cast<uintptr_t>(pSrcResource) }, SrcSubresource, nullptr,
		reshade::api::resource { reinterpret_cast<uintptr_t>(pDstResource) }, DstSubresource, nullptr, reshade::d3d11::convert_format(Format)))
		return;
#endif
	_orig->ResolveSubresource(pDstResource, DstSubresource, pSrcResource, SrcSubresource, Format);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::ExecuteCommandList(ID3D11CommandList *pCommandList, BOOL RestoreContextState)
{
	assert(pCommandList != nullptr);

	// The only way to create a command list is through 'FinishCommandList', so can always assume a proxy object here
	D3D11CommandList *const command_list_proxy = static_cast<D3D11CommandList *>(pCommandList);

#if RESHADE_ADDON
	reshade::invoke_addon_event<reshade::addon_event::execute_command_list>(this, command_list_proxy);
#endif

	// Get original command list pointer from proxy object and execute with it
	_orig->ExecuteCommandList(command_list_proxy->_orig, RestoreContextState);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::HSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
	_orig->HSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
#if RESHADE_ADDON
	invoke_bind_shader_resource_views_event(reshade::api::shader_stage::hull, StartSlot, NumViews, ppShaderResourceViews);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::HSSetShader(ID3D11HullShader *pHullShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
	_orig->HSSetShader(pHullShader, ppClassInstances, NumClassInstances);
#if RESHADE_ADDON
	reshade::invoke_addon_event<reshade::addon_event::bind_pipeline>(this, reshade::api::pipeline_type::graphics_hull_shader, reshade::api::pipeline { reinterpret_cast<uintptr_t>(pHullShader) });
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::HSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
	_orig->HSSetSamplers(StartSlot, NumSamplers, ppSamplers);
#if RESHADE_ADDON
	invoke_bind_samplers_event(reshade::api::shader_stage::hull, StartSlot, NumSamplers, ppSamplers);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::HSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
	_orig->HSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
#if RESHADE_ADDON
	invoke_bind_constant_buffers_event(reshade::api::shader_stage::hull, StartSlot, NumBuffers, ppConstantBuffers);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
	_orig->DSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
#if RESHADE_ADDON
	invoke_bind_shader_resource_views_event(reshade::api::shader_stage::domain, StartSlot, NumViews, ppShaderResourceViews);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DSSetShader(ID3D11DomainShader *pDomainShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
	_orig->DSSetShader(pDomainShader, ppClassInstances, NumClassInstances);
#if RESHADE_ADDON
	reshade::invoke_addon_event<reshade::addon_event::bind_pipeline>(this, reshade::api::pipeline_type::graphics_domain_shader, reshade::api::pipeline { reinterpret_cast<uintptr_t>(pDomainShader) });
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
	_orig->DSSetSamplers(StartSlot, NumSamplers, ppSamplers);
#if RESHADE_ADDON
	invoke_bind_samplers_event(reshade::api::shader_stage::domain, StartSlot, NumSamplers, ppSamplers);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
	_orig->DSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
#if RESHADE_ADDON
	invoke_bind_constant_buffers_event(reshade::api::shader_stage::domain, StartSlot, NumBuffers, ppConstantBuffers);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews)
{
	_orig->CSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
#if RESHADE_ADDON
	invoke_bind_shader_resource_views_event(reshade::api::shader_stage::compute, StartSlot, NumViews, ppShaderResourceViews);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CSSetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts)
{
	_orig->CSSetUnorderedAccessViews(StartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
#if RESHADE_ADDON
	invoke_bind_unordered_access_views_event(reshade::api::shader_stage::compute, StartSlot, NumUAVs, ppUnorderedAccessViews);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CSSetShader(ID3D11ComputeShader *pComputeShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances)
{
	_orig->CSSetShader(pComputeShader, ppClassInstances, NumClassInstances);
#if RESHADE_ADDON
	reshade::invoke_addon_event<reshade::addon_event::bind_pipeline>(this, reshade::api::pipeline_type::compute, reshade::api::pipeline { reinterpret_cast<uintptr_t>(pComputeShader) });
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers)
{
	_orig->CSSetSamplers(StartSlot, NumSamplers, ppSamplers);
#if RESHADE_ADDON
	invoke_bind_samplers_event(reshade::api::shader_stage::compute, StartSlot, NumSamplers, ppSamplers);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers)
{
	_orig->CSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
#if RESHADE_ADDON
	invoke_bind_constant_buffers_event(reshade::api::shader_stage::compute, StartSlot, NumBuffers, ppConstantBuffers);
#endif
}
void    STDMETHODCALLTYPE D3D11DeviceContext::VSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
	_orig->VSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::PSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
	_orig->PSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::PSGetShader(ID3D11PixelShader **ppPixelShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
	_orig->PSGetShader(ppPixelShader, ppClassInstances, pNumClassInstances);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::PSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
	_orig->PSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::VSGetShader(ID3D11VertexShader **ppVertexShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
	_orig->VSGetShader(ppVertexShader, ppClassInstances, pNumClassInstances);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::PSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
	_orig->PSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::IAGetInputLayout(ID3D11InputLayout **ppInputLayout)
{
	_orig->IAGetInputLayout(ppInputLayout);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::IAGetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppVertexBuffers, UINT *pStrides, UINT *pOffsets)
{
	_orig->IAGetVertexBuffers(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::IAGetIndexBuffer(ID3D11Buffer **pIndexBuffer, DXGI_FORMAT *Format, UINT *Offset)
{
	_orig->IAGetIndexBuffer(pIndexBuffer, Format, Offset);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::GSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
	_orig->GSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::GSGetShader(ID3D11GeometryShader **ppGeometryShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
	_orig->GSGetShader(ppGeometryShader, ppClassInstances, pNumClassInstances);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY *pTopology)
{
	_orig->IAGetPrimitiveTopology(pTopology);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::VSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
	_orig->VSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::VSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
	_orig->VSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::GetPredication(ID3D11Predicate **ppPredicate, BOOL *pPredicateValue)
{
	_orig->GetPredication(ppPredicate, pPredicateValue);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::GSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
	_orig->GSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::GSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
	_orig->GSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::OMGetRenderTargets(UINT NumViews, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView)
{
	_orig->OMGetRenderTargets(NumViews, ppRenderTargetViews, ppDepthStencilView);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::OMGetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView **ppUnorderedAccessViews)
{
	_orig->OMGetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRenderTargetViews, ppDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::OMGetBlendState(ID3D11BlendState **ppBlendState, FLOAT BlendFactor[4], UINT *pSampleMask)
{
	_orig->OMGetBlendState(ppBlendState, BlendFactor, pSampleMask);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::OMGetDepthStencilState(ID3D11DepthStencilState **ppDepthStencilState, UINT *pStencilRef)
{
	_orig->OMGetDepthStencilState(ppDepthStencilState, pStencilRef);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::SOGetTargets(UINT NumBuffers, ID3D11Buffer **ppSOTargets)
{
	_orig->SOGetTargets(NumBuffers, ppSOTargets);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::RSGetState(ID3D11RasterizerState **ppRasterizerState)
{
	_orig->RSGetState(ppRasterizerState);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::RSGetViewports(UINT *pNumViewports, D3D11_VIEWPORT *pViewports)
{
	_orig->RSGetViewports(pNumViewports, pViewports);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::RSGetScissorRects(UINT *pNumRects, D3D11_RECT *pRects)
{
	_orig->RSGetScissorRects(pNumRects, pRects);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::HSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
	_orig->HSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::HSGetShader(ID3D11HullShader **ppHullShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
	_orig->HSGetShader(ppHullShader, ppClassInstances, pNumClassInstances);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::HSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
	_orig->HSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::HSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
	_orig->HSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
	_orig->DSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DSGetShader(ID3D11DomainShader **ppDomainShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
	_orig->DSGetShader(ppDomainShader, ppClassInstances, pNumClassInstances);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
	_orig->DSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
	_orig->DSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews)
{
	_orig->CSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CSGetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView **ppUnorderedAccessViews)
{
	_orig->CSGetUnorderedAccessViews(StartSlot, NumUAVs, ppUnorderedAccessViews);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CSGetShader(ID3D11ComputeShader **ppComputeShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances)
{
	_orig->CSGetShader(ppComputeShader, ppClassInstances, pNumClassInstances);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers)
{
	_orig->CSGetSamplers(StartSlot, NumSamplers, ppSamplers);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers)
{
	_orig->CSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::ClearState()
{
	_orig->ClearState();
	// TODO: Call events with cleared state
}
void    STDMETHODCALLTYPE D3D11DeviceContext::Flush()
{
	_orig->Flush();
}
UINT    STDMETHODCALLTYPE D3D11DeviceContext::GetContextFlags()
{
	return _orig->GetContextFlags();
}
HRESULT STDMETHODCALLTYPE D3D11DeviceContext::FinishCommandList(BOOL RestoreDeferredContextState, ID3D11CommandList **ppCommandList)
{
#if RESHADE_ADDON
	if (_has_open_render_pass)
	{
		reshade::invoke_addon_event<reshade::addon_event::finish_render_pass>(this);
		_has_open_render_pass = false;
	}
#endif

	const HRESULT hr = _orig->FinishCommandList(RestoreDeferredContextState, ppCommandList);
	if (SUCCEEDED(hr))
	{
		assert(ppCommandList != nullptr);

		const auto command_list_proxy = new D3D11CommandList(_device, *ppCommandList);
		*ppCommandList = command_list_proxy;

#if RESHADE_ADDON
		reshade::invoke_addon_event<reshade::addon_event::execute_secondary_command_list>(command_list_proxy, this);
#endif
	}

	return hr;
}
D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE D3D11DeviceContext::GetType()
{
	return _orig->GetType();
}

void    STDMETHODCALLTYPE D3D11DeviceContext::CopySubresourceRegion1(ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox, UINT CopyFlags)
{
#if RESHADE_ADDON
	assert(pDstResource != nullptr && pSrcResource != nullptr);

	D3D11_RESOURCE_DIMENSION type;
	pDstResource->GetType(&type);
	if (type == D3D11_RESOURCE_DIMENSION_BUFFER)
	{
		assert(SrcSubresource == 0 && DstSubresource == 0);

		if (reshade::invoke_addon_event<reshade::addon_event::copy_buffer_region>(this,
			reshade::api::resource { reinterpret_cast<uintptr_t>(pSrcResource) }, pSrcBox != nullptr ? pSrcBox->left : 0,
			reshade::api::resource { reinterpret_cast<uintptr_t>(pDstResource) }, DstX, pSrcBox != nullptr ? pSrcBox->right - pSrcBox->left : ~0ull))
			return;
	}
	else
	{
		int32_t dst_box[6] = { static_cast<int32_t>(DstX), static_cast<int32_t>(DstY), static_cast<int32_t>(DstZ) };
		if (pSrcBox != nullptr)
		{
			dst_box[3] = dst_box[0] + pSrcBox->right - pSrcBox->left;
			dst_box[4] = dst_box[1] + pSrcBox->bottom - pSrcBox->top;
			dst_box[5] = dst_box[2] + pSrcBox->back - pSrcBox->front;
		}
		else
		{
			assert(DstX == 0 && DstY == 0 && DstZ == 0);
		}

		static_assert(sizeof(D3D11_BOX) == (sizeof(int32_t) * 6));

		if (reshade::invoke_addon_event<reshade::addon_event::copy_texture_region>(this,
			reshade::api::resource { reinterpret_cast<uintptr_t>(pSrcResource) }, SrcSubresource, reinterpret_cast<const int32_t *>(pSrcBox),
			reshade::api::resource { reinterpret_cast<uintptr_t>(pDstResource) }, DstSubresource, DstX != 0 || DstY != 0 || DstZ != 0 ? dst_box : nullptr, reshade::api::texture_filter::min_mag_mip_point))
			return;
	}
#endif

	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->CopySubresourceRegion1(pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox, CopyFlags);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::UpdateSubresource1(ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch, UINT CopyFlags)
{
#if RESHADE_ADDON
	assert(pDstResource != nullptr);

	D3D11_RESOURCE_DIMENSION type;
	pDstResource->GetType(&type);
	if (type == D3D11_RESOURCE_DIMENSION_BUFFER)
	{
		assert(DstSubresource == 0);

		if (reshade::invoke_addon_event<reshade::addon_event::upload_buffer_region>(_device,
			pSrcData,
			reshade::api::resource { reinterpret_cast<uintptr_t>(pDstResource) },
			pDstBox != nullptr ? pDstBox->left : 0,
			pDstBox != nullptr ? pDstBox->right - pDstBox->left : SrcRowPitch))
			return;
	}
	else
	{
		static_assert(sizeof(D3D11_BOX) == (sizeof(int32_t) * 6));

		if (reshade::invoke_addon_event<reshade::addon_event::upload_texture_region>(_device,
			reshade::api::subresource_data { pSrcData, SrcRowPitch, SrcDepthPitch },
			reshade::api::resource { reinterpret_cast<uintptr_t>(pDstResource) }, DstSubresource, reinterpret_cast<const int32_t *>(pDstBox)))
			return;
	}
#endif

	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->UpdateSubresource1(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch, CopyFlags);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DiscardResource(ID3D11Resource *pResource)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->DiscardResource(pResource);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DiscardView(ID3D11View *pResourceView)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->DiscardView(pResourceView);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::VSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->VSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::HSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->HSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->DSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::GSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->GSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::PSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->PSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CSSetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers, const UINT *pFirstConstant, const UINT *pNumConstants)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->CSSetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::VSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->VSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::HSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->HSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->DSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::GSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->GSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::PSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->PSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CSGetConstantBuffers1(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers, UINT *pFirstConstant, UINT *pNumConstants)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->CSGetConstantBuffers1(StartSlot, NumBuffers, ppConstantBuffers, pFirstConstant, pNumConstants);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::SwapDeviceContextState(ID3DDeviceContextState *pState, ID3DDeviceContextState **ppPreviousState)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->SwapDeviceContextState(pState, ppPreviousState);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::ClearView(ID3D11View *pView, const FLOAT Color[4], const D3D11_RECT *pRect, UINT NumRects)
{
#if RESHADE_ADDON
	if (com_ptr<ID3D11RenderTargetView> rtv;
		SUCCEEDED(pView->QueryInterface(&rtv)))
	{
		if (const reshade::api::resource_view rtv_handle = reshade::api::resource_view { reinterpret_cast<uintptr_t>(rtv.get()) };
			reshade::invoke_addon_event<reshade::addon_event::clear_render_target_views>(this, 1, &rtv_handle, Color))
			return;
	}
	if (com_ptr<ID3D11DepthStencilView> dsv;
		SUCCEEDED(pView->QueryInterface(&dsv)))
	{
		// The 'ID3D11DeviceContext1::ClearView' API only works on depth-stencil views to depth-only resources (with no stencil component)
		if (reshade::invoke_addon_event<reshade::addon_event::clear_depth_stencil_view>(this, reshade::api::resource_view { reinterpret_cast<uintptr_t>(dsv.get()) }, 0x1, Color[0], static_cast<uint8_t>(0)))
			return;
	}
	if (com_ptr<ID3D11UnorderedAccessView> uav;
		SUCCEEDED(pView->QueryInterface(&uav)))
	{
		if (reshade::invoke_addon_event<reshade::addon_event::clear_unordered_access_view_float>(this, reshade::api::resource_view { reinterpret_cast<uintptr_t>(uav.get()) }, Color))
			return;
	}
#endif

	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->ClearView(pView, Color, pRect, NumRects);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::DiscardView1(ID3D11View *pResourceView, const D3D11_RECT *pRects, UINT NumRects)
{
	assert(_interface_version >= 1);
	static_cast<ID3D11DeviceContext1 *>(_orig)->DiscardView1(pResourceView, pRects, NumRects);
}

HRESULT STDMETHODCALLTYPE D3D11DeviceContext::UpdateTileMappings(ID3D11Resource *pTiledResource, UINT NumTiledResourceRegions, const D3D11_TILED_RESOURCE_COORDINATE *pTiledResourceRegionStartCoordinates, const D3D11_TILE_REGION_SIZE *pTiledResourceRegionSizes, ID3D11Buffer *pTilePool, UINT NumRanges, const UINT *pRangeFlags, const UINT *pTilePoolStartOffsets, const UINT *pRangeTileCounts, UINT Flags)
{
	assert(_interface_version >= 2);
	return static_cast<ID3D11DeviceContext2 *>(_orig)->UpdateTileMappings(pTiledResource, NumTiledResourceRegions, pTiledResourceRegionStartCoordinates, pTiledResourceRegionSizes, pTilePool, NumRanges, pRangeFlags, pTilePoolStartOffsets, pRangeTileCounts, Flags);
}
HRESULT STDMETHODCALLTYPE D3D11DeviceContext::CopyTileMappings(ID3D11Resource *pDestTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pDestRegionStartCoordinate, ID3D11Resource *pSourceTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pSourceRegionStartCoordinate, const D3D11_TILE_REGION_SIZE *pTileRegionSize, UINT Flags)
{
	assert(_interface_version >= 2);
	return static_cast<ID3D11DeviceContext2 *>(_orig)->CopyTileMappings(pDestTiledResource, pDestRegionStartCoordinate, pSourceTiledResource, pSourceRegionStartCoordinate, pTileRegionSize, Flags);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::CopyTiles(ID3D11Resource *pTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pTileRegionStartCoordinate, const D3D11_TILE_REGION_SIZE *pTileRegionSize, ID3D11Buffer *pBuffer, UINT64 BufferStartOffsetInBytes, UINT Flags)
{
	assert(_interface_version >= 2);
	static_cast<ID3D11DeviceContext2 *>(_orig)->CopyTiles(pTiledResource, pTileRegionStartCoordinate, pTileRegionSize, pBuffer, BufferStartOffsetInBytes, Flags);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::UpdateTiles(ID3D11Resource *pDestTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pDestTileRegionStartCoordinate, const D3D11_TILE_REGION_SIZE *pDestTileRegionSize, const void *pSourceTileData, UINT Flags)
{
	assert(_interface_version >= 2);
	static_cast<ID3D11DeviceContext2 *>(_orig)->UpdateTiles(pDestTiledResource, pDestTileRegionStartCoordinate, pDestTileRegionSize, pSourceTileData, Flags);
}
HRESULT STDMETHODCALLTYPE D3D11DeviceContext::ResizeTilePool(ID3D11Buffer *pTilePool, UINT64 NewSizeInBytes)
{
	assert(_interface_version >= 2);
	return static_cast<ID3D11DeviceContext2 *>(_orig)->ResizeTilePool(pTilePool, NewSizeInBytes);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::TiledResourceBarrier(ID3D11DeviceChild *pTiledResourceOrViewAccessBeforeBarrier, ID3D11DeviceChild *pTiledResourceOrViewAccessAfterBarrier)
{
	assert(_interface_version >= 2);
	static_cast<ID3D11DeviceContext2 *>(_orig)->TiledResourceBarrier(pTiledResourceOrViewAccessBeforeBarrier, pTiledResourceOrViewAccessAfterBarrier);
}
BOOL    STDMETHODCALLTYPE D3D11DeviceContext::IsAnnotationEnabled()
{
	assert(_interface_version >= 2);
	return static_cast<ID3D11DeviceContext2 *>(_orig)->IsAnnotationEnabled();
}
void    STDMETHODCALLTYPE D3D11DeviceContext::SetMarkerInt(LPCWSTR pLabel, INT Data)
{
	assert(_interface_version >= 2);
	static_cast<ID3D11DeviceContext2 *>(_orig)->SetMarkerInt(pLabel, Data);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::BeginEventInt(LPCWSTR pLabel, INT Data)
{
	assert(_interface_version >= 2);
	static_cast<ID3D11DeviceContext2 *>(_orig)->BeginEventInt(pLabel, Data);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::EndEvent()
{
	assert(_interface_version >= 2);
	static_cast<ID3D11DeviceContext2 *>(_orig)->EndEvent();
}

void    STDMETHODCALLTYPE D3D11DeviceContext::Flush1(D3D11_CONTEXT_TYPE ContextType, HANDLE hEvent)
{
	assert(_interface_version >= 3);
	static_cast<ID3D11DeviceContext3 *>(_orig)->Flush1(ContextType, hEvent);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::SetHardwareProtectionState(BOOL HwProtectionEnable)
{
	assert(_interface_version >= 3);
	static_cast<ID3D11DeviceContext3 *>(_orig)->SetHardwareProtectionState(HwProtectionEnable);
}
void    STDMETHODCALLTYPE D3D11DeviceContext::GetHardwareProtectionState(BOOL *pHwProtectionEnable)
{
	assert(_interface_version >= 3);
	static_cast<ID3D11DeviceContext3 *>(_orig)->GetHardwareProtectionState(pHwProtectionEnable);
}

HRESULT STDMETHODCALLTYPE D3D11DeviceContext::Signal(ID3D11Fence *pFence, UINT64 Value)
{
	assert(_interface_version >= 4);
	return static_cast<ID3D11DeviceContext4 *>(_orig)->Signal(pFence, Value);
}
HRESULT STDMETHODCALLTYPE D3D11DeviceContext::Wait(ID3D11Fence *pFence, UINT64 Value)
{
	assert(_interface_version >= 4);
	return static_cast<ID3D11DeviceContext4 *>(_orig)->Wait(pFence, Value);
}
