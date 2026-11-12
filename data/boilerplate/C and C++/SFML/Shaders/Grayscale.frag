---
order: 2
description: Draws a sprite or texture in shades of grey
file: shaders/grayscale.frag
whole-file: yes
---
// Draws a sprite in shades of grey:
// shader.setUniform("texture", sf::Shader::CurrentTexture);  window.draw(sprite, &shader);
uniform sampler2D texture;

void main()
{
    vec4 pixel = texture2D(texture, gl_TexCoord[0].xy);

    // Weighted, because the eye sees green as brighter than red and red as brighter than blue.
    float grey = dot(pixel.rgb, vec3(0.299, 0.587, 0.114));
    gl_FragColor = vec4(vec3(grey), pixel.a) * gl_Color;
}
