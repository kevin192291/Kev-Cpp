---
order: 50
section: sound and network
description: Loads a sound effect into memory and streams looping background music from assets/
packages: sfml-audio
sfml: 3
includes: <SFML/Audio.hpp> <iostream>
---
// Before the game loop. Short sounds are loaded into a buffer, which
// must outlive the sounds that play it; music streams from its file while it plays. WAV, OGG, FLAC and MP3 work.
sf::SoundBuffer jumpBuffer;
if (!jumpBuffer.loadFromFile("assets/jump.wav"))
{
    std::cerr << "Can't load assets/jump.wav (paths are relative to the working directory)\n";
    return 1;
}
sf::Sound jumpSound(jumpBuffer);
jumpSound.setVolume(80.f); // 0 to 100

sf::Music music;
if (!music.openFromFile("assets/music.ogg"))
{
    std::cerr << "Can't open assets/music.ogg (paths are relative to the working directory)\n";
    return 1;
}
music.setLooping(true);
music.setVolume(40.f);
music.play();
{{cursor}}
// On an event:  jumpSound.play();  A sf::Sound plays one copy at a time; make several for overlapping copies.
// Pause the music with music.pause(); check music.getStatus() == sf::SoundSource::Status::Playing.
