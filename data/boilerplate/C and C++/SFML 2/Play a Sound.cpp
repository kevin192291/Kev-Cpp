---
order: 12
description: Loads a sound file and plays it (SFML 2)
includes: <SFML/Audio.hpp>
packages: sfml-audio
sfml: 2
---
// A sound buffer has to live as long as the sounds that play it.
sf::SoundBuffer buffer;
if (!buffer.loadFromFile("sound.wav"))
{
    return 1;
}
sf::Sound sound(buffer);
sound.play();
