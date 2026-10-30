cbuffer Constants : register(b0) {
    float4x4 previousViewProjection;
    float2 inverseResolution;
    uint historyValid;
    float historyWeight;
};
Texture2D<float4> currentColor : register(t0);
Texture2D<float4> worldPosition : register(t1);
Texture2D<float4> normalData : register(t2);
Texture2D<float4> historyColor : register(t3);
Texture2D<float4> historyPosition : register(t4);
SamplerState linearClamp : register(s0);
struct Varying { float4 position:SV_POSITION;float2 uv:TEXCOORD0; };
Varying VSMain(uint id:SV_VertexID) {
    Varying o;float2 uv=float2((id<<1)&2,id&2);
    o.position=float4(uv*float2(2,-2)+float2(-1,1),0,1);o.uv=uv;return o;
}
float4 PSMain(Varying i):SV_TARGET {
    int2 pixel=int2(i.position.xy);
    float3 current=currentColor.Load(int3(pixel,0)).rgb;
    float4 world=worldPosition.Load(int3(pixel,0));
    // No object motion vectors yet: reject animated particle surfaces and sky.
    if(historyValid==0 || world.w<0.5 || normalData.Load(int3(pixel,0)).w<0.5)
        return float4(current,1);
    float4 previousClip=mul(float4(world.xyz,1),previousViewProjection);
    if(previousClip.w<=0) return float4(current,1);
    float3 ndc=previousClip.xyz/previousClip.w;
    float2 uv=ndc.xy*float2(0.5,-0.5)+0.5;
    if(any(uv<inverseResolution*0.5)||any(uv>1-inverseResolution*0.5)||ndc.z<0||ndc.z>1)
        return float4(current,1);
    uint w,h;historyPosition.GetDimensions(w,h);
    int2 previousPixel=clamp(int2(uv*float2(w,h)),int2(0,0),int2(w-1,h-1));
    float4 oldPosition=historyPosition.Load(int3(previousPixel,0));
    float footprint=max(length(ddx(world.xyz)),length(ddy(world.xyz)));
    if(oldPosition.w<0.5 || distance(oldPosition.xyz,world.xyz)>max(0.15,footprint*2))
        return float4(current,1);
    float3 lo=current,hi=current;
    [unroll]for(int y=-1;y<=1;++y) [unroll]for(int x=-1;x<=1;++x) {
        int2 p=clamp(pixel+int2(x,y),int2(0,0),int2(w-1,h-1));
        float3 c=currentColor.Load(int3(p,0)).rgb;lo=min(lo,c);hi=max(hi,c);
    }
    float3 history=clamp(historyColor.SampleLevel(linearClamp,uv,0).rgb,lo,hi);
    float motion=length((uv-i.uv)/inverseResolution);
    float weight=lerp(historyWeight,0.65,saturate(motion/32));
    // Accumulate compressed HDR to reduce the influence of isolated bright samples.
    float3 a=current/(1+max(current.r,max(current.g,current.b)));
    float3 b=history/(1+max(history.r,max(history.g,history.b)));
    float3 result=lerp(a,b,weight);
    result/=max(1-max(result.r,max(result.g,result.b)),0.0001);
    return float4(result,1);
}
