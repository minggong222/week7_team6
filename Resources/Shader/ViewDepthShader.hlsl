cbuffer ObjectConstants : register(b0)
{
    row_major matrix World;
};
cbuffer ViewConstants : register(b2)
{
    row_major matrix ViewProjection;
};
cbuffer ViewDepthConstants : register(b5)
{
    row_major matrix View;
};
struct VS_INPUT
{
    float3 Position : POSITION;
};
struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float ViewDepth : TEXCOORD0;
};
PS_INPUT mainVS(VS_INPUT Input)
{
    PS_INPUT Output;
    float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);
    Output.Position = mul(WorldPosition, ViewProjection);
    Output.ViewDepth = mul(WorldPosition, View).x;
    return Output;
}
float mainPS(PS_INPUT Input) : SV_TARGET
{
    return Input.ViewDepth;
}
