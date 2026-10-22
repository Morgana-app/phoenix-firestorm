#include "ReShade.fxh"
// Dedicated test only. No depth input, sharpening, tone mapping or temporal state.
sampler2D FS_P1Source {
    Texture = ReShade::BackBufferTex;
    MinFilter = POINT; MagFilter = POINT; MipFilter = POINT;
    AddressU = CLAMP; AddressV = CLAMP;
    SRGBTexture = false;
};
float4 FS_P1Pixel(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target {
    return tex2D(FS_P1Source, uv);
}
technique FS_P1Identity {
    pass {
        VertexShader = PostProcessVS;
        PixelShader = FS_P1Pixel;
        BlendEnable = false;
        SRGBWriteEnable = false;
    }
}
