---
order: 1
description: An animated colour plasma; set the time and resolution uniforms every frame
file: shaders/plasma.frag
whole-file: yes
---
// An animated plasma for a rectangle covering the window (SFML → Fragment Shader uses it).
// shader.setUniform("time", seconds);  shader.setUniform("resolution", sf::Vector2f(window.getSize()));
uniform float time;
uniform vec2 resolution;

void main()
{
    vec2 uv = gl_FragCoord.xy / resolution * 10.0;

    float v = sin(uv.x + time);
    v += sin((uv.y + time) * 0.5);
    v += sin((uv.x + uv.y + time) * 0.5);
    vec2 c = uv + vec2(sin(time / 3.0), cos(time / 2.0)) * 5.0;
    v += sin(sqrt(c.x * c.x + c.y * c.y + 1.0) + time);
    v *= 0.5;

    // Three sine waves a third of a turn apart give a smooth rainbow.
    const float pi = 3.14159265;
    gl_FragColor = vec4(0.5 + 0.5 * sin(pi * v),
                        0.5 + 0.5 * sin(pi * v + 2.0 * pi / 3.0),
                        0.5 + 0.5 * sin(pi * v + 4.0 * pi / 3.0),
                        1.0);
}
