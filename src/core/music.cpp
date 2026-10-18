#include "core/music.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/bytes.h"
#include "core/file.h"
#include "core/ini.h"
#include "core/log.h"
#include "core/text.h"
#include "platform/platform.h"

#include "dr_mp3/dr_mp3.h"
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION
#include "stb_vorbis/stb_vorbis.c"

namespace m360 {

namespace {

constexpr uint32_t OUTPUT_RATE = 48000;
// Frames decoded at a time, and at most per update: about 85 ms, more than a frame plays, so the queue fills up in a
// few frames without one long decode.
constexpr size_t CHUNK = 1024, UPDATE_FRAMES = 4096;

uint32_t tag(const char *s)
{
	return (uint32_t)(uint8_t)s[0] | (uint32_t)(uint8_t)s[1] << 8 | (uint32_t)(uint8_t)s[2] << 16 |
		(uint32_t)(uint8_t)s[3] << 24;
}

class Mp3Source : public MusicSource {
public:
	bool open(const std::vector<uint8_t> &bytes)
	{
		if (!drmp3_init_memory(&mp3_, bytes.data(), bytes.size(), nullptr))
			return false;
		opened_ = true;
		rate = mp3_.sampleRate;
		channels = mp3_.channels;
		return rate > 0 && channels > 0;
	}
	~Mp3Source() override
	{
		if (opened_)
			drmp3_uninit(&mp3_);
	}
	size_t read(float *out, size_t frames) override { return (size_t)drmp3_read_pcm_frames_f32(&mp3_, frames, out); }
	bool seek(uint32_t frame) override { return drmp3_seek_to_pcm_frame(&mp3_, frame) != 0; }

private:
	drmp3 mp3_{};
	bool opened_ = false;
};

class VorbisSource : public MusicSource {
public:
	bool open(const std::vector<uint8_t> &bytes)
	{
		int error = 0;
		vorbis_ = stb_vorbis_open_memory(bytes.data(), (int)bytes.size(), &error, nullptr);
		if (!vorbis_)
			return false;
		const stb_vorbis_info info = stb_vorbis_get_info(vorbis_);
		rate = info.sample_rate;
		channels = (uint32_t)info.channels;
		return rate > 0 && channels > 0;
	}
	~VorbisSource() override
	{
		if (vorbis_)
			stb_vorbis_close(vorbis_);
	}
	size_t read(float *out, size_t frames) override
	{
		return (size_t)stb_vorbis_get_samples_float_interleaved(vorbis_, (int)channels, out, (int)(frames * channels));
	}
	bool seek(uint32_t frame) override { return stb_vorbis_seek(vorbis_, frame) != 0; }

private:
	stb_vorbis *vorbis_ = nullptr;
};

// PCM WAV, 8 or 16 bits, or 32-bit floating point, read as it plays.
class WaveSource : public MusicSource {
public:
	bool open(const std::vector<uint8_t> &bytes)
	{
		ByteReader r(bytes);
		if (r.u32() != tag("RIFF"))
			return false;
		r.u32();
		if (r.u32() != tag("WAVE"))
			return false;
		while (r.ok() && r.pos() + 8 <= r.size()) {
			const uint32_t kind = r.u32(), size = r.u32();
			const size_t start = r.pos();
			if (kind == tag("fmt ") && size >= 16) {
				format_ = r.u16();
				channels = r.u16();
				rate = r.u32();
				r.u32();
				r.u16();
				bits_ = r.u16();
			} else if (kind == tag("data")) {
				data_ = r.at(start, size);
				bytes_ = data_ ? size : 0;
			}
			r.seek(start + size + (size & 1));
		}
		const bool pcm = format_ == 1 && (bits_ == 8 || bits_ == 16), real = format_ == 3 && bits_ == 32;
		if (!(pcm || real) || channels == 0 || rate == 0 || !data_)
			return false;
		frameBytes_ = channels * (bits_ / 8);
		frames_ = (uint32_t)(bytes_ / frameBytes_);
		return true;
	}
	size_t read(float *out, size_t frames) override
	{
		const size_t count = std::min<size_t>(frames, frames_ - std::min(position_, frames_));
		ByteReader r(data_, bytes_);
		r.seek((size_t)position_ * frameBytes_);
		for (size_t i = 0; i < count * channels; i++) {
			if (bits_ == 8)
				out[i] = ((int)r.u8() - 128) / 128.0f;
			else if (bits_ == 16)
				out[i] = r.s16() / 32768.0f;
			else {
				// The value is put together from its little-endian bytes first, so this works on the 360 too.
				const uint32_t bits = r.u32();
				std::memcpy(&out[i], &bits, sizeof bits);
			}
		}
		position_ += (uint32_t)count;
		return count;
	}
	bool seek(uint32_t frame) override
	{
		if (frame > frames_)
			return false;
		position_ = frame;
		return true;
	}

private:
	const uint8_t *data_ = nullptr;
	size_t bytes_ = 0, frameBytes_ = 1;
	uint16_t format_ = 0, bits_ = 0;
	uint32_t frames_ = 0, position_ = 0;
};

bool hasExtension(const std::string &path, const char *extension)
{
	const std::string lower = text::toLower(path);
	const size_t length = std::char_traits<char>::length(extension);
	return lower.size() >= length && lower.compare(lower.size() - length, length, extension) == 0;
}

} // namespace

void MusicTrack::readMotif(const std::string &prefix, const IniSection &section)
{
	path = section.text(prefix);
	section.readInt(prefix + ".volume", volume);
	int32_t value = 1;
	if (section.readInt(prefix + ".loop", value))
		loop = value != 0;
	section.readInt(prefix + ".loopstart", loopStart);
	section.readInt(prefix + ".loopend", loopEnd);
}

void MusicTrack::readStage(const IniSection &section)
{
	path = section.text("bgmusic");
	section.readInt("bgmvolume", volume);
	int32_t value = 1;
	if (section.readInt("bgmloop", value))
		loop = value != 0;
	section.readInt("bgmloopstart", loopStart);
	section.readInt("bgmloopend", loopEnd);
}

// Ikemen's SearchFile(file, {def, "", "data/"}, "sound/"): next to each root, then in its sound/ folder, then the
// name as it is. A file that isn't anywhere keeps its name, and fails to open when played.
void MusicTrack::resolve(const std::string &defPath)
{
	std::string file(text::trim(path));
	std::replace(file.begin(), file.end(), '\\', '/');
	path = file;
	if (file.empty())
		return;
	const bool inSound = text::startsWith(text::toLower(file), "sound/");
	for (const std::string &base : { directoryOf(defPath), std::string(), std::string("data/") }) {
		if (fileExists(base + file)) {
			path = base + file;
			return;
		}
		if (!inSound && fileExists(base + "sound/" + file)) {
			path = base + "sound/" + file;
			return;
		}
	}
}

std::unique_ptr<MusicSource> openMusic(const std::string &path, const std::vector<uint8_t> &bytes)
{
	if (hasExtension(path, ".mp3")) {
		auto source = std::make_unique<Mp3Source>();
		if (source->open(bytes))
			return source;
	} else if (hasExtension(path, ".ogg")) {
		auto source = std::make_unique<VorbisSource>();
		if (source->open(bytes))
			return source;
	} else if (hasExtension(path, ".wav")) {
		auto source = std::make_unique<WaveSource>();
		if (source->open(bytes))
			return source;
	}
	return nullptr;
}

float Music::gain(int32_t volume)
{
	const double level = -5 + 75 * 0.06 * volume / 100.0;
	return level <= -5 ? 0.0f : (float)std::pow(2.0, std::min(level, 1.0));
}

void Music::request(const MusicTrack &track, bool interrupt)
{
	if (!track.path.empty() && text::toLower(track.path) == text::toLower(track_.path)) {
		volume_ = gain(track.volume);
		return;
	}
	if (track.path.empty()) {
		if (interrupt)
			stop();
		return;
	}
	restart(track);
}

void Music::restart(const MusicTrack &track)
{
	stop();
	track_ = track;
	if (track.path.empty())
		return;
	std::vector<uint8_t> bytes;
	if (!readFile(track.path, bytes)) {
		logf("music: can't open %s", track.path.c_str());
		return;
	}
	bytes_ = std::move(bytes);
	std::unique_ptr<MusicSource> source = openMusic(track.path, bytes_);
	if (!source) {
		logf("music: %s isn't an MP3, Ogg Vorbis or WAV file that decodes", track.path.c_str());
		bytes_.clear();
		return;
	}
	logf("music: %s, %u Hz, %u channels", track.path.c_str(), (unsigned)source->rate, (unsigned)source->channels);
	playSource(std::move(source), track);
}

void Music::playSource(std::unique_ptr<MusicSource> source, const MusicTrack &track)
{
	track_ = track;
	source_ = std::move(source);
	ended_ = false;
	volume_ = gain(track.volume);
	position_ = 0;
	chunk_.assign(CHUNK * std::max<uint32_t>(1, source_->channels), 0.0f);
	chunkFrames_ = chunkAt_ = 0;
	step_ = (double)source_->rate / OUTPUT_RATE;
	fraction_ = 0;
	primed_ = false;
}

void Music::stop()
{
	platform::clearMusic();
	source_.reset();
	bytes_.clear();
	bytes_.shrink_to_fit();
	track_ = MusicTrack{};
	track_.path.clear();
	ended_ = true;
}

bool Music::nextFrame(float &left, float &right)
{
	if (chunkAt_ >= chunkFrames_) {
		const uint32_t loopStart = (uint32_t)std::max(0, track_.loopStart);
		const uint32_t loopEnd = track_.loop && track_.loopEnd > 0 ? (uint32_t)track_.loopEnd : 0;
		if (loopEnd > 0 && position_ >= loopEnd) {
			if (!source_->seek(loopStart))
				return false;
			position_ = loopStart;
		}
		size_t want = CHUNK;
		if (loopEnd > position_)
			want = std::min<size_t>(want, loopEnd - position_);
		chunkFrames_ = source_->read(chunk_.data(), want);
		chunkAt_ = 0;
		if (chunkFrames_ == 0) {
			// The end of the file: back to the loop start, or the song is over.
			if (!track_.loop || !source_->seek(loopStart))
				return false;
			position_ = loopStart;
			chunkFrames_ = source_->read(chunk_.data(), want);
			if (chunkFrames_ == 0)
				return false;
		}
	}
	const float *frame = &chunk_[chunkAt_ * source_->channels];
	left = frame[0];
	right = source_->channels > 1 ? frame[1] : frame[0];
	chunkAt_++;
	position_++;
	return true;
}

size_t Music::produce(float *out, size_t frames)
{
	if (!source_ || ended_)
		return 0;
	if (!primed_) {
		if (!nextFrame(previous_[0], previous_[1])) {
			ended_ = true;
			return 0;
		}
		if (!nextFrame(current_[0], current_[1])) {
			current_[0] = previous_[0];
			current_[1] = previous_[1];
		}
		primed_ = true;
	}
	size_t made = 0;
	while (made < frames) {
		while (fraction_ >= 1) {
			previous_[0] = current_[0];
			previous_[1] = current_[1];
			if (!nextFrame(current_[0], current_[1])) {
				ended_ = true;
				return made;
			}
			fraction_ -= 1;
		}
		const float f = (float)fraction_;
		out[made * 2] = (previous_[0] + (current_[0] - previous_[0]) * f) * volume_;
		out[made * 2 + 1] = (previous_[1] + (current_[1] - previous_[1]) * f) * volume_;
		fraction_ += step_;
		made++;
	}
	return made;
}

void Music::update()
{
	if (!source_ || ended_)
		return;
	const size_t space = std::min(platform::musicSpace(), UPDATE_FRAMES);
	if (space == 0)
		return;
	output_.resize(space * 2);
	const size_t made = produce(output_.data(), space);
	platform::queueMusic(output_.data(), made);
}

} // namespace m360
