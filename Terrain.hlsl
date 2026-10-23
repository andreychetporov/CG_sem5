cbuffer TerrainConstants : register(b0) { float4x4 viewProjection; float lod; float debugLOD; float2 padding; };
struct VSOut { float4 position:SV_POSITION;float3 world:TEXCOORD0;float3 normal:NORMAL; };
VSOut VSMain(float3 position:POSITION,float3 normal:NORMAL) {
    VSOut o;o.position=mul(float4(position,1),viewProjection);o.world=position;o.normal=normal;return o;
}
struct Targets {float4 position:SV_TARGET0;float4 normal:SV_TARGET1;float4 albedo:SV_TARGET2;};
Targets PSMain(VSOut i) {
    Targets o;float3 n=normalize(i.normal);
    float3 grass=float3(0.12,0.27,0.055),rock=float3(0.31,0.27,0.22);
    float3 color=lerp(grass,rock,saturate((1-n.y)*4));
    color=lerp(color,float3(0.8,0.83,0.86),smoothstep(175,210,i.world.y));
    color*=0.94+0.06*sin(i.world.x*0.7)*cos(i.world.z*0.5);
    if(debugLOD>0.5) {const float3 colors[5]={float3(1,0,0),float3(1,0.6,0),float3(0,0.7,0),float3(0,0.4,1),float3(0.7,0,1)};color=colors[(int)lod];}
    o.position=float4(i.world,1);o.normal=float4(n,1);o.albedo=float4(color,1);return o;
}
