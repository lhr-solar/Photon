#version 450

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec4 inColor;
layout (location = 3) in vec3 inViewVec;
layout (location = 4) in vec3 inLightVec;
layout (location = 5) in vec4 inEffectColor;

layout (binding = 3) uniform sampler2D baseColorTexture;

layout (location = 0) out vec4 outFragColor;

layout (push_constant) uniform ModelPC {
	mat4 transform;
	vec4 effectColor;
	vec4 materialColor; // glTF baseColorFactor; defaults to 1 when no texture
	int effectType;
} pc;

void main() 
{
	vec3 N = normalize(inNormal);
	vec3 L = normalize(inLightVec);
	vec3 V = normalize(inViewVec);
	vec3 R = reflect(-L, N);
	
	// Sample base color texture (will be white if using default texture)
	vec4 textureColor = texture(baseColorTexture, inUV);
	
	// Ensure texture has valid alpha (some textures might have 0 alpha)
	if (textureColor.a < 0.01) {
		textureColor = vec4(1.0); // Use white if texture is fully transparent
	}
	
	// Lighting calculations
	float ambient = 0.3; // Add ambient lighting so model is always visible
	float diffuse = max(dot(N, L), 0.0);
	vec3 specular = pow(max(dot(R, V), 0.0), 16.0) * vec3(0.75);
	
	// Combine texture color with vertex color and lighting
	vec4 baseColor = textureColor * inColor * pc.materialColor;
    baseColor.rgb = baseColor.rgb * (ambient + diffuse) + specular;
    
    outFragColor = baseColor * inEffectColor;
    
    if(pc.effectType == 1){
        outFragColor.rgb = vec3(1.0) - outFragColor.rgb;
    } else if(pc.effectType == 2){
        float g = dot(outFragColor.rgb, vec3(0.299, 0.587, 0.114));
        outFragColor = vec4(g, g, g, outFragColor.a);
    }
}
