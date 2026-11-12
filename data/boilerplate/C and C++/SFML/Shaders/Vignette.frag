---
order: 3
description: Darkens the edges of a texture, for example the whole scene drawn into a sf::RenderTexture
file: shaders/vignette.frag
whole-file: yes
---
// Darkens the edges, e.g. of the scene drawn into a sf::RenderTexture and shown with a sprite:
// shader.setUniform("texture", sf::Shader::CurrentTexture);  shader.setUniform("strength", 0.7f);
// window.draw(sceneSprite, &shader);
uniform sampler2D texture;
uniform float strength; // 0 = no effect, 1 = black corners

void main()
{
    vec2 uv = gl_TexCoord[0].xy;
    vec4 pixel = texture2D(texture, uv);

    float shade = 1.0 - smoothstep(0.3, 0.8, distance(uv, vec2(0.5))) * strength;
    gl_FragColor = vec4(pixel.rgb * shade, pixel.a) * gl_Color;
}
