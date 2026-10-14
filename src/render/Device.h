#pragma once

#include "render/Com.h"

#include <array>
#include <map>
#include <memory>
#include <vector>

namespace bw::render {

class DirectDraw;
class Surface;

/// IDirect3DDevice7 on Direct3D 9. Keeps the Direct3D 7 values of every state (GetRenderState and friends return what
/// the game set, state blocks replay Direct3D 7 calls) and translates each change to Direct3D 9 (render/Translate.h).
class Device final : public IDirect3DDevice7 {
public:
    static HRESULT Create(DirectDraw& owner, Surface& target, Device** out) noexcept;

    STDMETHOD(QueryInterface)(REFIID iid, LPVOID* out) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(GetCaps)(LPD3DDEVICEDESC7 desc) override;
    STDMETHOD(EnumTextureFormats)(LPD3DENUMPIXELFORMATSCALLBACK callback, LPVOID context) override;
    STDMETHOD(BeginScene)() override;
    STDMETHOD(EndScene)() override;
    STDMETHOD(GetDirect3D)(LPDIRECT3D7* out) override;
    STDMETHOD(SetRenderTarget)(LPDIRECTDRAWSURFACE7 target, DWORD flags) override;
    STDMETHOD(GetRenderTarget)(LPDIRECTDRAWSURFACE7* out) override;
    STDMETHOD(Clear)(DWORD count, LPD3DRECT rects, DWORD flags, D3DCOLOR color, D3DVALUE z, DWORD stencil) override;
    STDMETHOD(SetTransform)(D3DTRANSFORMSTATETYPE type, LPD3DMATRIX matrix) override;
    STDMETHOD(GetTransform)(D3DTRANSFORMSTATETYPE type, LPD3DMATRIX matrix) override;
    STDMETHOD(SetViewport)(LPD3DVIEWPORT7 viewport) override;
    STDMETHOD(MultiplyTransform)(D3DTRANSFORMSTATETYPE type, LPD3DMATRIX matrix) override;
    STDMETHOD(GetViewport)(LPD3DVIEWPORT7 viewport) override;
    STDMETHOD(SetMaterial)(LPD3DMATERIAL7 material) override;
    STDMETHOD(GetMaterial)(LPD3DMATERIAL7 material) override;
    STDMETHOD(SetLight)(DWORD index, LPD3DLIGHT7 light) override;
    STDMETHOD(GetLight)(DWORD index, LPD3DLIGHT7 light) override;
    STDMETHOD(SetRenderState)(D3DRENDERSTATETYPE state, DWORD value) override;
    STDMETHOD(GetRenderState)(D3DRENDERSTATETYPE state, LPDWORD value) override;
    STDMETHOD(BeginStateBlock)() override;
    STDMETHOD(EndStateBlock)(LPDWORD handle) override;
    STDMETHOD(PreLoad)(LPDIRECTDRAWSURFACE7) override;
    STDMETHOD(DrawPrimitive)(D3DPRIMITIVETYPE type, DWORD fvf, LPVOID vertices, DWORD count, DWORD flags) override;
    STDMETHOD(DrawIndexedPrimitive)(D3DPRIMITIVETYPE type, DWORD fvf, LPVOID vertices, DWORD vertexCount,
                                    LPWORD indices, DWORD indexCount, DWORD flags) override;
    STDMETHOD(SetClipStatus)(LPD3DCLIPSTATUS status) override;
    STDMETHOD(GetClipStatus)(LPD3DCLIPSTATUS status) override;
    STDMETHOD(DrawPrimitiveStrided)(D3DPRIMITIVETYPE, DWORD, LPD3DDRAWPRIMITIVESTRIDEDDATA, DWORD, DWORD) override BW_UNSUPPORTED
    STDMETHOD(DrawIndexedPrimitiveStrided)(D3DPRIMITIVETYPE, DWORD, LPD3DDRAWPRIMITIVESTRIDEDDATA, DWORD, LPWORD, DWORD,
                                           DWORD) override BW_UNSUPPORTED
    STDMETHOD(DrawPrimitiveVB)(D3DPRIMITIVETYPE, LPDIRECT3DVERTEXBUFFER7, DWORD, DWORD, DWORD) override BW_UNSUPPORTED
    STDMETHOD(DrawIndexedPrimitiveVB)(D3DPRIMITIVETYPE, LPDIRECT3DVERTEXBUFFER7, DWORD, DWORD, LPWORD, DWORD,
                                      DWORD) override BW_UNSUPPORTED
    STDMETHOD(ComputeSphereVisibility)(LPD3DVECTOR, LPD3DVALUE, DWORD, DWORD, LPDWORD) override BW_UNSUPPORTED
    STDMETHOD(GetTexture)(DWORD stage, LPDIRECTDRAWSURFACE7* out) override;
    STDMETHOD(SetTexture)(DWORD stage, LPDIRECTDRAWSURFACE7 texture) override;
    STDMETHOD(GetTextureStageState)(DWORD stage, D3DTEXTURESTAGESTATETYPE type, LPDWORD value) override;
    STDMETHOD(SetTextureStageState)(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) override;
    STDMETHOD(ValidateDevice)(LPDWORD passes) override;
    STDMETHOD(ApplyStateBlock)(DWORD handle) override;
    STDMETHOD(CaptureStateBlock)(DWORD handle) override;
    STDMETHOD(DeleteStateBlock)(DWORD handle) override;
    STDMETHOD(CreateStateBlock)(D3DSTATEBLOCKTYPE type, LPDWORD handle) override;
    STDMETHOD(Load)(LPDIRECTDRAWSURFACE7, LPPOINT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD) override BW_UNSUPPORTED
    STDMETHOD(LightEnable)(DWORD index, BOOL enable) override;
    STDMETHOD(GetLightEnable)(DWORD index, BOOL* enable) override;
    STDMETHOD(SetClipPlane)(DWORD, D3DVALUE*) override BW_UNSUPPORTED
    STDMETHOD(GetClipPlane)(DWORD, D3DVALUE*) override BW_UNSUPPORTED
    STDMETHOD(GetInfo)(DWORD, LPVOID, DWORD) override;

    /// Called when a texture surface is destroyed while bound.
    void Unbind(const Surface& surface) noexcept;

private:
    static constexpr std::size_t kRenderStates = 256;
    static constexpr std::size_t kStages = 8;
    static constexpr std::size_t kStageStates = 32;

    /// A recorded state block: the Direct3D 7 calls to replay.
    struct StateBlock;

    Device(DirectDraw& owner, Surface& target) noexcept;
    ~Device();

    void ApplyRenderState(DWORD state, DWORD value) noexcept;
    void ApplyStageState(DWORD stage, DWORD type, DWORD value) noexcept;
    void SetTarget(Surface& target) noexcept;

    RefCount m_references;
    DirectDraw& m_owner;
    Surface* m_target = nullptr;

    std::array<DWORD, kRenderStates> m_renderStates{};
    std::array<std::array<DWORD, kStageStates>, kStages> m_stageStates{};
    std::array<Surface*, kStages> m_textures{};
    std::map<DWORD, D3DMATRIX> m_transforms;
    D3DVIEWPORT7 m_viewport{};
    D3DMATERIAL7 m_material{};
    std::map<DWORD, D3DLIGHT7> m_lights;
    std::map<DWORD, BOOL> m_lightEnabled;
    D3DCLIPSTATUS m_clipStatus{};

    std::vector<std::unique_ptr<StateBlock>> m_stateBlocks;  // handle = index + 1
    StateBlock* m_recording = nullptr;
};

/// The device of the running game (one at a time), for surfaces that need to unbind themselves.
Device* CurrentDevice() noexcept;

}  // namespace bw::render
