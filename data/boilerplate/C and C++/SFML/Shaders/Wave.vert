---
order: 4
description: A vertex shader that makes a mesh wave like a flag; also shows what a vertex shader must output
file: shaders/wave.vert
whole-file: yes
---
// Moves each vertex along a wave. A sprite has only 4 corners, so draw something with many vertices
// (a sf::VertexArray grid of triangles). Load it with a fragment shader, or alone with Type::Vertex:
// shader.setUniform("time", seconds);  shader.setUniform("amplitude", sf::Vector2f(10.f, 5.f));
uniform float time;
uniform vec2 amplitude; // in pixels

void main()
{
    vec4 vertex = gl_Vertex;
    vertex.x += cos(gl_Vertex.y * 0.02 + time * 3.8) * amplitude.x;
    vertex.y += sin(gl_Vertex.x * 0.02 + time * 2.4) * amplitude.y;

    // Every SFML vertex shader passes these on: position, texture coordinates and colour.
    gl_Position = gl_ModelViewProjectionMatrix * vertex;
    gl_TexCoord[0] = gl_TextureMatrix[0] * gl_MultiTexCoord0;
    gl_FrontColor = gl_Color;
}
