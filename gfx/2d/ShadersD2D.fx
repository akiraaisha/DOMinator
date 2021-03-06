// We store vertex coordinates and the quad shape in a constant buffer, this is
// easy to update and allows us to use a single call to set the x, y, w, h of
// the quad.
// The QuadDesc and TexCoords both work as follows:
// The x component is the quad left point, the y component is the top point
// the z component is the width, and the w component is the height. The quad
// are specified in viewport coordinates, i.e. { -1.0f, 1.0f, 2.0f, -2.0f }
// would cover the entire viewport (which runs from <-1.0f, 1.0f> left to right
// and <-1.0f, 1.0f> -bottom- to top. The TexCoords desc is specified in texture
// space <0, 1.0f> left to right and top to bottom. The input vertices of the
// shader stage always form a rectangle from {0, 0} - {1, 1}
cbuffer cb0
{
    float4 QuadDesc;
    float4 TexCoords;
    float4 MaskTexCoords;
}

cbuffer cb1
{
    float4 BlurOffsetsH[3];
    float4 BlurOffsetsV[3];
    float4 BlurWeights[3];
    float4 ShadowColor;
}

cbuffer cb2
{
    float3x3 DeviceSpaceToUserSpace;
    float2 dimensions;
    // Precalculate as much as we can!
    float3 diff;
    float2 center1;
    float A;
    float radius1;
    float sq_radius1;
}

struct VS_OUTPUT
{
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
    float2 MaskTexCoord : TEXCOORD1;
};

struct VS_RADIAL_OUTPUT
{
    float4 Position : SV_Position;
    float2 MaskTexCoord : TEXCOORD0;
    float2 PixelCoord : TEXCOORD1;
};

Texture2D tex;
Texture2D mask;

sampler sSampler = sampler_state {
    Filter = MIN_MAG_MIP_LINEAR;
    Texture = tex;
    AddressU = Clamp;
    AddressV = Clamp;
};

sampler sMaskSampler = sampler_state {
    Filter = MIN_MAG_MIP_LINEAR;
    Texture = mask;
    AddressU = Clamp;
    AddressV = Clamp;
};

sampler sShadowSampler = sampler_state {
    Filter = MIN_MAG_MIP_LINEAR;
    Texture = tex;
    AddressU = Border;
    AddressV = Border;
    BorderColor = float4(0, 0, 0, 0);
};

RasterizerState TextureRast
{
  ScissorEnable = False;
  CullMode = None;
};

BlendState ShadowBlendH
{
  BlendEnable[0] = False;
  RenderTargetWriteMask[0] = 0xF;
};

BlendState ShadowBlendV
{
  BlendEnable[0] = True;
  SrcBlend = One;
  DestBlend = Inv_Src_Alpha;
  BlendOp = Add;
  SrcBlendAlpha = One;
  DestBlendAlpha = Inv_Src_Alpha;
  BlendOpAlpha = Add;
  RenderTargetWriteMask[0] = 0xF;
};

VS_OUTPUT SampleTextureVS(float3 pos : POSITION)
{
    VS_OUTPUT Output;
    Output.Position.w = 1.0f;
    Output.Position.x = pos.x * QuadDesc.z + QuadDesc.x;
    Output.Position.y = pos.y * QuadDesc.w + QuadDesc.y;
    Output.Position.z = 0;
    Output.TexCoord.x = pos.x * TexCoords.z + TexCoords.x;
    Output.TexCoord.y = pos.y * TexCoords.w + TexCoords.y;
    Output.MaskTexCoord.x = pos.x * MaskTexCoords.z + MaskTexCoords.x;
    Output.MaskTexCoord.y = pos.y * MaskTexCoords.w + MaskTexCoords.y;
    return Output;
}

VS_RADIAL_OUTPUT SampleRadialVS(float3 pos : POSITION)
{
    VS_RADIAL_OUTPUT Output;
    Output.Position.w = 1.0f;
    Output.Position.x = pos.x * QuadDesc.z + QuadDesc.x;
    Output.Position.y = pos.y * QuadDesc.w + QuadDesc.y;
    Output.Position.z = 0;
    Output.MaskTexCoord.x = pos.x * MaskTexCoords.z + MaskTexCoords.x;
    Output.MaskTexCoord.y = pos.y * MaskTexCoords.w + MaskTexCoords.y;

    // For the radial gradient pixel shader we need to pass in the pixel's
    // coordinates in user space for the color to be correctly determined.

    Output.PixelCoord.x = ((Output.Position.x + 1.0f) / 2.0f) * dimensions.x;
    Output.PixelCoord.y = ((1.0f - Output.Position.y) / 2.0f) * dimensions.y;
    Output.PixelCoord.xy = mul(float3(Output.PixelCoord.x, Output.PixelCoord.y, 1.0f), DeviceSpaceToUserSpace).xy;
    return Output;
}

float4 SampleTexturePS( VS_OUTPUT In) : SV_Target
{
    return tex.Sample(sSampler, In.TexCoord);
};

float4 SampleMaskTexturePS( VS_OUTPUT In) : SV_Target
{
    return tex.Sample(sSampler, In.TexCoord) * mask.Sample(sMaskSampler, In.MaskTexCoord).a;
};

float4 SampleRadialGradientPS( VS_RADIAL_OUTPUT In) : SV_Target
{
    // Radial gradient painting is defined as the set of circles whose centers
    // are described by C(t) = (C2 - C1) * t + C1; with radii
    // R(t) = (R2 - R1) * t + R1; for R(t) > 0. This shader solves the
    // quadratic equation that arises when calculating t for pixel (x, y).
    //
    // A more extensive derrivation can be found in the pixman radial gradient
    // code.
 
    float2 p = In.PixelCoord;
    float3 dp = float3(p - center1, radius1);

    // dpx * dcx + dpy * dcy + r * dr
    float B = dot(dp, diff);

    float C = pow(dp.x, 2) + pow(dp.y, 2) - sq_radius1;

    float det = pow(B, 2) - A * C;

    if (det < 0) {
      return float4(0, 0, 0, 0);
    }

    float sqrt_det = sqrt(abs(det));

    float2 t = (B + float2(sqrt_det, -sqrt_det)) / A;

    float2 isValid = step(float2(-radius1, -radius1), t * diff.z);

    if (max(isValid.x, isValid.y) <= 0) {
      return float4(0, 0, 0, 0);
    }

    float upper_t = lerp(t.y, t.x, isValid.x);

    float4 output = tex.Sample(sSampler, float2(upper_t, 0.5));
    // Premultiply
    output.rgb *= output.a;
    // Multiply the output color by the input mask for the operation.
    output *= mask.Sample(sMaskSampler, In.MaskTexCoord).a;
    return output;
};

float4 SampleRadialGradientA0PS( VS_RADIAL_OUTPUT In) : SV_Target
{
    // This simpler shader is used for the degenerate case where A is 0,
    // i.e. we're actually solving a linear equation.

    float2 p = In.PixelCoord;
    float3 dp = float3(p - center1, radius1);

    // dpx * dcx + dpy * dcy + r * dr
    float B = dot(dp, diff);

    float C = pow(dp.x, 2) + pow(dp.y, 2) - pow(radius1, 2);

    float t = 0.5 * C / B;

    if (-radius1 >= t * diff.z) {
      return float4(0, 0, 0, 0);
    }

    float4 output = tex.Sample(sSampler, float2(t, 0.5));
    // Premultiply
    output.rgb *= output.a;
    // Multiply the output color by the input mask for the operation.
    output *= mask.Sample(sMaskSampler, In.MaskTexCoord).a;
    return output;
};

float4 SampleShadowHPS( VS_OUTPUT In) : SV_Target
{
    float outputStrength = 0;

    outputStrength += BlurWeights[0].x * tex.Sample(sShadowSampler, float2(In.TexCoord.x + BlurOffsetsH[0].x, In.TexCoord.y)).a;
    outputStrength += BlurWeights[0].y * tex.Sample(sShadowSampler, float2(In.TexCoord.x + BlurOffsetsH[0].y, In.TexCoord.y)).a;
    outputStrength += BlurWeights[0].z * tex.Sample(sShadowSampler, float2(In.TexCoord.x + BlurOffsetsH[0].z, In.TexCoord.y)).a;
    outputStrength += BlurWeights[0].w * tex.Sample(sShadowSampler, float2(In.TexCoord.x + BlurOffsetsH[0].w, In.TexCoord.y)).a;
    outputStrength += BlurWeights[1].x * tex.Sample(sShadowSampler, float2(In.TexCoord.x + BlurOffsetsH[1].x, In.TexCoord.y)).a;
    outputStrength += BlurWeights[1].y * tex.Sample(sShadowSampler, float2(In.TexCoord.x + BlurOffsetsH[1].y, In.TexCoord.y)).a;
    outputStrength += BlurWeights[1].z * tex.Sample(sShadowSampler, float2(In.TexCoord.x + BlurOffsetsH[1].z, In.TexCoord.y)).a;
    outputStrength += BlurWeights[1].w * tex.Sample(sShadowSampler, float2(In.TexCoord.x + BlurOffsetsH[1].w, In.TexCoord.y)).a;
    outputStrength += BlurWeights[2].x * tex.Sample(sShadowSampler, float2(In.TexCoord.x + BlurOffsetsH[2].x, In.TexCoord.y)).a;

    return ShadowColor * outputStrength;
};

float4 SampleShadowVPS( VS_OUTPUT In) : SV_Target
{
    float4 outputColor = float4(0, 0, 0, 0);

    outputColor += BlurWeights[0].x * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[0].x));
    outputColor += BlurWeights[0].y * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[0].y));
    outputColor += BlurWeights[0].z * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[0].z));
    outputColor += BlurWeights[0].w * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[0].w));
    outputColor += BlurWeights[1].x * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[1].x));
    outputColor += BlurWeights[1].y * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[1].y));
    outputColor += BlurWeights[1].z * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[1].z));
    outputColor += BlurWeights[1].w * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[1].w));
    outputColor += BlurWeights[2].x * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[2].x));

    return outputColor;
};

float4 SampleMaskShadowVPS( VS_OUTPUT In) : SV_Target
{
    float4 outputColor = float4(0, 0, 0, 0);

    outputColor += BlurWeights[0].x * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[0].x));
    outputColor += BlurWeights[0].y * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[0].y));
    outputColor += BlurWeights[0].z * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[0].z));
    outputColor += BlurWeights[0].w * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[0].w));
    outputColor += BlurWeights[1].x * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[1].x));
    outputColor += BlurWeights[1].y * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[1].y));
    outputColor += BlurWeights[1].z * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[1].z));
    outputColor += BlurWeights[1].w * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[1].w));
    outputColor += BlurWeights[2].x * tex.Sample(sShadowSampler, float2(In.TexCoord.x, In.TexCoord.y + BlurOffsetsV[2].x));

    return outputColor * mask.Sample(sMaskSampler, In.MaskTexCoord).a;
};

technique10 SampleTexture
{
    pass P0
    {
        SetRasterizerState(TextureRast);
        SetVertexShader(CompileShader(vs_4_0_level_9_3, SampleTextureVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_4_0_level_9_3, SampleTexturePS()));
    }
}

technique10 SampleRadialGradient
{
    pass P0
    {
        SetRasterizerState(TextureRast);
        SetVertexShader(CompileShader(vs_4_0_level_9_3, SampleRadialVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_4_0_level_9_3, SampleRadialGradientPS()));
    }
    pass P1
    {
        SetRasterizerState(TextureRast);
        SetVertexShader(CompileShader(vs_4_0_level_9_3, SampleRadialVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_4_0_level_9_3, SampleRadialGradientA0PS()));
    }
}

technique10 SampleMaskedTexture
{
    pass P0
    {
        SetRasterizerState(TextureRast);
        SetVertexShader(CompileShader(vs_4_0_level_9_3, SampleTextureVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_4_0_level_9_3, SampleMaskTexturePS()));
    }
}

technique10 SampleTextureWithShadow
{
    // Horizontal pass
    pass P0
    {
        SetRasterizerState(TextureRast);
        SetBlendState(ShadowBlendH, float4(1.0f, 1.0f, 1.0f, 1.0f), 0xffffffff);
        SetVertexShader(CompileShader(vs_4_0_level_9_3, SampleTextureVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_4_0_level_9_3, SampleShadowHPS()));
    }
    // Vertical pass
    pass P1
    {
        SetRasterizerState(TextureRast);
        SetBlendState(ShadowBlendV, float4(1.0f, 1.0f, 1.0f, 1.0f), 0xffffffff);
        SetVertexShader(CompileShader(vs_4_0_level_9_3, SampleTextureVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_4_0_level_9_3, SampleShadowVPS()));
    }
    // Vertical pass - used when using a mask
    pass P2
    {
        SetRasterizerState(TextureRast);
        SetBlendState(ShadowBlendV, float4(1.0f, 1.0f, 1.0f, 1.0f), 0xffffffff);
        SetVertexShader(CompileShader(vs_4_0_level_9_3, SampleTextureVS()));
        SetGeometryShader(NULL);
        SetPixelShader(CompileShader(ps_4_0_level_9_3, SampleMaskShadowVPS()));
    }
 }
