#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace m360 {

struct IniSection;

// A song and how to play it: a [Music] entry ("title.bgm", ".volume", ".loop", ".loopstart", ".loopend") or a stage's
// bgmusic / bgmvolume / bgmloopstart / bgmloopend (Ikemen's bgMusic). The loop points are in the file's own samples;
// a loop end of 0 is the end of the file.
struct MusicTrack {
	std::string path;
	int32_t volume = 100;
	bool loop = true;
	int32_t loopStart = 0, loopEnd = 0;

	// Reads "<prefix>" (the file) and "<prefix>.volume / .loop / .loopstart / .loopend", for a screenpack's [Music].
	void readMotif(const std::string &prefix, const IniSection &section);
	// Reads a stage's [Music] (bgmusic, bgmvolume, bgmloop, bgmloopstart, bgmloopend).
	void readStage(const IniSection &section);
	// Finds the file as Ikemen's SearchFile does: next to the def, as given, under data/, under sound/. Empty if it's
	// not there (logged once per name).
	void resolve(const std::string &defPath);
};

// A decoder for one file in memory: interleaved floating-point frames at the file's own rate.
class MusicSource {
public:
	virtual ~MusicSource() = default;
	// Up to frames frames into out (frames * channels() floats); 0 at the end.
	virtual size_t read(float *out, size_t frames) = 0;
	virtual bool seek(uint32_t frame) = 0;
	uint32_t rate = 0, channels = 0;
};

// Opens a file's bytes as MP3, Ogg Vorbis or WAV by its extension; null if it's none of them or won't decode. The
// source keeps a pointer to the bytes, which must outlive it.
std::unique_ptr<MusicSource> openMusic(const std::string &path, const std::vector<uint8_t> &bytes);

// Ikemen's Bgm: the one song playing, decoded a little at a time into the platform's music queue (a whole song
// decoded would take tens of MB), turned to 48 kHz stereo, at Ikemen's volume for the BGM setting (75) and the song's
// volume, looping between its loop points. MIDI and tracker files aren't supported.
class Music {
public:
	// Ikemen's playBgm: the same file keeps playing; another file replaces it; no file stops the music when
	// interrupting (the title and select screens), and otherwise leaves it playing (the VS screen).
	void request(const MusicTrack &track, bool interrupt);
	// Plays the track from its start, even if it's playing already (a fight's music at round 1).
	void restart(const MusicTrack &track);
	void stop();
	// Tops up the platform's queue; once a frame.
	void update();
	const std::string &playing() const { return track_.path; }

	// Ikemen's Bgm.UpdateVolume as a gain: 2 ^ (-5 + BGMVolume 75 * 0.06 * volume / 100), silent at -5 or below.
	static float gain(int32_t volume);
	// The options' Master and BGM volumes (0-100) that gain uses.
	static void setVolumes(int32_t master, int32_t bgm);
	// Fills out with frames 48 kHz stereo frames from the song (the queue's side of update), for the tests.
	size_t produce(float *out, size_t frames);
	// Plays a source already opened, for the tests.
	void playSource(std::unique_ptr<MusicSource> source, const MusicTrack &track);

private:
	// The next frame of the song at its own rate, looping as it says; false once it has ended.
	bool nextFrame(float &left, float &right);

	MusicTrack track_;
	std::vector<uint8_t> bytes_;
	std::unique_ptr<MusicSource> source_;
	bool ended_ = true;
	float volume_ = 0;
	// Where the song is, in its own frames, and a chunk of it decoded.
	uint32_t position_ = 0;
	std::vector<float> chunk_;
	size_t chunkFrames_ = 0, chunkAt_ = 0;
	// Linear resampling to 48 kHz: the two frames either side and how far between them.
	float previous_[2] = { 0, 0 }, current_[2] = { 0, 0 };
	double fraction_ = 0, step_ = 1;
	bool primed_ = false;
	std::vector<float> output_;
};

} // namespace m360
