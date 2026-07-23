#include "Reanimation.h"

#include "GamePacker/GamePacker.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

#include <zlib.h>

#ifdef _MSC_VER
#ifdef _DEBUG
#pragma comment(lib, "zlibd.lib")
#else
#pragma comment(lib, "zlib.lib")
#endif
#endif

namespace {
	const std::uint32_t kCompiledMagic = 0xDEADFED4u;
	const std::uint32_t kRawMagic = 0xB393B4C0u;
	const std::uint32_t kRawHeaderSentinel = 0x0Cu;
	const std::uint32_t kTrackSeparator = 0x2Cu;
	const float kMissingFloat = -10000.0f;
	const std::size_t kMaximumFileSize = 512u * 1024u * 1024u;
	const std::size_t kMaximumStringSize = 16u * 1024u * 1024u;
	const std::size_t kMaximumTrackCount = 100000u;
	const std::size_t kMaximumTransformCount = 10000000u;

	struct ParsedReanimation {
		float fps = 0.0f;
		std::vector<sgf::TrackInfo> tracks;
		std::set<sgf::String> images;
	};

	void SetError(sgf::String& error, const sgf::String& message)
	{
		if (error.empty())
			error = message;
	}

	std::uint32_t ReadLittleEndianU32(const unsigned char* bytes)
	{
		return static_cast<std::uint32_t>(bytes[0]) |
			(static_cast<std::uint32_t>(bytes[1]) << 8) |
			(static_cast<std::uint32_t>(bytes[2]) << 16) |
			(static_cast<std::uint32_t>(bytes[3]) << 24);
	}

	bool IsValidUtf8(const sgf::String& value)
	{
		for (std::size_t index = 0; index < value.size();) {
			const unsigned char first = static_cast<unsigned char>(value[index]);
			if (first == 0)
				return false;
			if (first <= 0x7F) {
				++index;
				continue;
			}

			std::size_t length = 0;
			std::uint32_t codePoint = 0;
			if (first >= 0xC2 && first <= 0xDF) {
				length = 2;
				codePoint = first & 0x1Fu;
			}
			else if (first >= 0xE0 && first <= 0xEF) {
				length = 3;
				codePoint = first & 0x0Fu;
			}
			else if (first >= 0xF0 && first <= 0xF4) {
				length = 4;
				codePoint = first & 0x07u;
			}
			else {
				return false;
			}

			if (index + length > value.size())
				return false;
			for (std::size_t item = 1; item < length; ++item) {
				const unsigned char continuation = static_cast<unsigned char>(value[index + item]);
				if ((continuation & 0xC0u) != 0x80u)
					return false;
				codePoint = (codePoint << 6) | (continuation & 0x3Fu);
			}

			if ((length == 3 && codePoint < 0x800u) ||
				(length == 4 && codePoint < 0x10000u) ||
				(codePoint >= 0xD800u && codePoint <= 0xDFFFu) ||
				codePoint > 0x10FFFFu)
				return false;
			index += length;
		}
		return true;
	}

	class BinaryReader {
	public:
		BinaryReader(const std::vector<unsigned char>& data, sgf::String& error)
			: mData(data), mError(error)
		{
		}

		bool ReadU32(std::uint32_t& value, const sgf::String& label)
		{
			if (!Require(4, label))
				return false;
			value = ReadLittleEndianU32(&mData[mOffset]);
			mOffset += 4;
			return true;
		}

		bool ReadI32(std::int32_t& value, const sgf::String& label)
		{
			std::uint32_t bits = 0;
			if (!ReadU32(bits, label))
				return false;
			std::memcpy(&value, &bits, sizeof(value));
			return true;
		}

		bool ReadFloat(float& value, const sgf::String& label)
		{
			std::uint32_t bits = 0;
			if (!ReadU32(bits, label))
				return false;
			std::memcpy(&value, &bits, sizeof(value));
			if (!std::isfinite(value)) {
				SetError(mError, "non-finite float in " + label);
				return false;
			}
			return true;
		}

		bool ReadString(sgf::String& value, const sgf::String& label)
		{
			std::int32_t signedLength = 0;
			if (!ReadI32(signedLength, label + " length"))
				return false;
			if (signedLength < 0) {
				SetError(mError, "negative string length in " + label);
				return false;
			}
			const std::size_t length = static_cast<std::size_t>(signedLength);
			if (length > kMaximumStringSize) {
				SetError(mError, "excessive string length in " + label);
				return false;
			}
			if (!Require(length, label))
				return false;
			if (length == 0)
				value.clear();
			else
				value.assign(reinterpret_cast<const char*>(&mData[mOffset]), length);
			mOffset += length;
			if (!IsValidUtf8(value)) {
				SetError(mError, "invalid UTF-8 in " + label);
				return false;
			}
			return true;
		}

		bool Skip(std::size_t length, const sgf::String& label)
		{
			if (!Require(length, label))
				return false;
			mOffset += length;
			return true;
		}

		std::size_t Remaining() const
		{
			return mData.size() - mOffset;
		}

	private:
		bool Require(std::size_t length, const sgf::String& label)
		{
			if (length > Remaining()) {
				std::ostringstream message;
				message << "truncated " << label << " at offset 0x" << std::hex << mOffset;
				SetError(mError, message.str());
				return false;
			}
			return true;
		}

		const std::vector<unsigned char>& mData;
		sgf::String& mError;
		std::size_t mOffset = 0;
	};

	sgf::TrackFrameTransform DefaultTransform()
	{
		sgf::TrackFrameTransform transform;
		transform.x = 0.0f;
		transform.y = 0.0f;
		transform.kx = 0.0f;
		transform.ky = 0.0f;
		transform.sx = 1.0f;
		transform.sy = 1.0f;
		transform.a = 1.0f;
		transform.f = 0;
		transform.i.clear();
		transform.font.clear();
		transform.text.clear();
		return transform;
	}

	bool ReadFileBytes(const char* filePath, std::vector<unsigned char>& bytes, sgf::String& error)
	{
		if (!filePath || !*filePath) {
			SetError(error, "Reanimation path is empty");
			return false;
		}

		SDL_RWops* file = SDL_RWFromFile(filePath, "rb");
		if (file) {
			const Sint64 signedFileSize = SDL_RWsize(file);
			if (signedFileSize < 0 || static_cast<std::uint64_t>(signedFileSize) > kMaximumFileSize) {
				SDL_RWclose(file);
				SetError(error, "Reanimation file exceeds the 512 MiB safety limit");
				return false;
			}
			const std::size_t fileSize = static_cast<std::size_t>(signedFileSize);
			bytes.resize(fileSize);
			const std::size_t bytesRead = fileSize == 0 ? 0 :
				SDL_RWread(file, &bytes[0], 1, fileSize);
			SDL_RWclose(file);
			if (bytesRead != fileSize) {
				bytes.clear();
				SetError(error, "could not read the complete Reanimation file");
				return false;
			}
			return true;
		}

		// Keep package-backed XML resources compatible with the existing framework.
		sgf::FileStream* stream = sgf::FileManager::TryToLoadFilePointer(filePath);
		if (!stream) {
			SetError(error, "failed to open Reanimation: " + sgf::String(SDL_GetError()));
			return false;
		}
		const int signedSize = stream->GetSize();
		if (signedSize < 0 || static_cast<std::size_t>(signedSize) > kMaximumFileSize) {
			delete stream;
			SetError(error, "invalid or excessive Reanimation file size");
			return false;
		}
		bytes.resize(static_cast<std::size_t>(signedSize));
		if (!bytes.empty())
			stream->Read(&bytes[0], signedSize);
		delete stream;
		return true;
	}

	bool InflateCompiled(const std::vector<unsigned char>& source,
		std::vector<unsigned char>& raw, sgf::String& error)
	{
		if (source.size() < 8 || ReadLittleEndianU32(&source[0]) != kCompiledMagic) {
			SetError(error, "invalid compiled Reanimation header");
			return false;
		}
		const std::uint32_t expectedSize = ReadLittleEndianU32(&source[4]);
		if (expectedSize == 0 || expectedSize > kMaximumFileSize) {
			SetError(error, "invalid compiled Reanimation uncompressed size");
			return false;
		}
		const std::size_t compressedSize = source.size() - 8;
		if (compressedSize == 0 || compressedSize > std::numeric_limits<uInt>::max()) {
			SetError(error, "invalid compiled Reanimation compressed size");
			return false;
		}

		raw.resize(expectedSize);
		z_stream inflater = {};
		inflater.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(&source[8]));
		inflater.avail_in = static_cast<uInt>(compressedSize);
		inflater.next_out = reinterpret_cast<Bytef*>(&raw[0]);
		inflater.avail_out = static_cast<uInt>(raw.size());
		if (inflateInit(&inflater) != Z_OK) {
			SetError(error, "could not initialize zlib for compiled Reanimation");
			return false;
		}

		const int status = inflate(&inflater, Z_FINISH);
		const bool complete = status == Z_STREAM_END &&
			inflater.total_out == expectedSize && inflater.avail_in == 0;
		inflateEnd(&inflater);
		if (!complete) {
			SetError(error, "compiled Reanimation zlib payload is truncated or has trailing data");
			raw.clear();
			return false;
		}
		return true;
	}

	void ApplyFloat(float serialized, float& destination)
	{
		if (serialized != kMissingFloat)
			destination = serialized;
	}

	bool ApplyFrameFlag(float serialized, int& destination, sgf::String& error,
		const sgf::String& label)
	{
		if (serialized == kMissingFloat)
			return true;
		if (serialized < static_cast<float>(std::numeric_limits<int>::min()) ||
			serialized > static_cast<float>(std::numeric_limits<int>::max()) ||
			std::trunc(serialized) != serialized) {
			SetError(error, "non-integral frame visibility flag in " + label);
			return false;
		}
		destination = static_cast<int>(serialized);
		return true;
	}

	bool ParseCompiledRaw(const std::vector<unsigned char>& raw,
		ParsedReanimation& parsed, sgf::String& error)
	{
		BinaryReader reader(raw, error);
		std::uint32_t rawMagic = 0;
		if (!reader.ReadU32(rawMagic, "raw magic") || rawMagic != kRawMagic) {
			SetError(error, "invalid PC Reanimation raw magic");
			return false;
		}
		if (!reader.Skip(4, "raw version word"))
			return false;

		std::int32_t signedTrackCount = 0;
		if (!reader.ReadI32(signedTrackCount, "track count") || signedTrackCount <= 0 ||
			static_cast<std::size_t>(signedTrackCount) > kMaximumTrackCount) {
			SetError(error, "invalid PC Reanimation track count");
			return false;
		}
		if (!reader.ReadFloat(parsed.fps, "fps") || parsed.fps <= 0.0f) {
			SetError(error, "invalid PC Reanimation fps");
			return false;
		}
		if (!reader.Skip(4, "raw reserved header word"))
			return false;
		std::uint32_t headerSentinel = 0;
		if (!reader.ReadU32(headerSentinel, "raw header sentinel") ||
			headerSentinel != kRawHeaderSentinel) {
			SetError(error, "invalid PC Reanimation raw header sentinel");
			return false;
		}

		const std::size_t trackCount = static_cast<std::size_t>(signedTrackCount);
		std::vector<std::size_t> transformCounts(trackCount);
		std::size_t totalTransformCount = 0;
		for (std::size_t trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
			if (!reader.Skip(8, "track header reserved words"))
				return false;
			std::int32_t signedTransformCount = 0;
			if (!reader.ReadI32(signedTransformCount, "track transform count") ||
				signedTransformCount <= 0) {
				SetError(error, "invalid transform count in track " + std::to_string(trackIndex));
				return false;
			}
			const std::size_t transformCount = static_cast<std::size_t>(signedTransformCount);
			if (transformCount > kMaximumTransformCount - totalTransformCount) {
				SetError(error, "PC Reanimation has too many transforms");
				return false;
			}
			transformCounts[trackIndex] = transformCount;
			totalTransformCount += transformCount;
		}
		if (totalTransformCount > reader.Remaining() / 44u) {
			SetError(error, "transform counts exceed the remaining PC Reanimation payload");
			return false;
		}

		parsed.tracks.reserve(trackCount);
		std::size_t expectedFrameCount = transformCounts[0];
		for (std::size_t trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
			if (transformCounts[trackIndex] != expectedFrameCount) {
				SetError(error, "tracks have inconsistent frame counts");
				return false;
			}

			sgf::TrackInfo track;
			if (!reader.ReadString(track.mTrackName,
				"track[" + std::to_string(trackIndex) + "] name"))
				return false;
			std::uint32_t separator = 0;
			if (!reader.ReadU32(separator, "track separator") || separator != kTrackSeparator) {
				SetError(error, "invalid separator in track " + std::to_string(trackIndex));
				return false;
			}

			const std::size_t transformCount = transformCounts[trackIndex];
			track.mFrames.reserve(transformCount);
			sgf::TrackFrameTransform state = DefaultTransform();
			for (std::size_t transformIndex = 0; transformIndex < transformCount; ++transformIndex) {
				float fields[8] = {};
				for (std::size_t field = 0; field < 8; ++field) {
					if (!reader.ReadFloat(fields[field], "transform numeric field"))
						return false;
				}
				// These are serialized resource pointers/slots, not transform values.
				if (!reader.Skip(12, "transform reserved resource slots"))
					return false;

				ApplyFloat(fields[0], state.x);
				ApplyFloat(fields[1], state.y);
				ApplyFloat(fields[2], state.kx);
				ApplyFloat(fields[3], state.ky);
				ApplyFloat(fields[4], state.sx);
				ApplyFloat(fields[5], state.sy);
				if (!ApplyFrameFlag(fields[6], state.f, error,
					"track " + std::to_string(trackIndex) + " transform " + std::to_string(transformIndex)))
					return false;
				ApplyFloat(fields[7], state.a);
				track.mFrames.push_back(state);
			}

			sgf::String image;
			sgf::String font;
			sgf::String text;
			for (std::size_t transformIndex = 0; transformIndex < transformCount; ++transformIndex) {
				sgf::String serializedImage;
				sgf::String serializedFont;
				sgf::String serializedText;
				const sgf::String label = "track[" + std::to_string(trackIndex) + "].transform[" +
					std::to_string(transformIndex) + "]";
				if (!reader.ReadString(serializedImage, label + ".image") ||
					!reader.ReadString(serializedFont, label + ".font") ||
					!reader.ReadString(serializedText, label + ".text"))
					return false;
				if (!serializedImage.empty())
					image = serializedImage;
				if (!serializedFont.empty())
					font = serializedFont;
				if (!serializedText.empty())
					text = serializedText;
				track.mFrames[transformIndex].i = image;
				track.mFrames[transformIndex].font = font;
				track.mFrames[transformIndex].text = text;
				if (!image.empty())
					parsed.images.insert(image);
			}
			parsed.tracks.push_back(std::move(track));
		}

		if (reader.Remaining() != 0) {
			SetError(error, "PC Reanimation has trailing raw bytes");
			return false;
		}
		return true;
	}

	bool ParseXml(const std::vector<unsigned char>& source,
		ParsedReanimation& parsed, sgf::String& error)
	{
		pugi::xml_document document;
		const pugi::xml_parse_result result = document.load_buffer(
			source.empty() ? nullptr : &source[0], source.size(), pugi::parse_default,
			pugi::encoding_utf8);
		if (!result) {
			SetError(error, "XML parse error: " + sgf::String(result.description()));
			return false;
		}

		bool hasFps = false;
		for (pugi::xml_node node : document.children()) {
			const sgf::String tagName = node.name();
			if (tagName == "fps") {
				parsed.fps = node.text().as_float();
				hasFps = true;
			}
			else if (tagName == "track") {
				sgf::TrackInfo track;
				sgf::TrackFrameTransform state = DefaultTransform();
				for (pugi::xml_node child : node.children()) {
					const sgf::String childTag = child.name();
					if (childTag == "name") {
						track.mTrackName = child.text().as_string();
					}
					else if (childTag == "t") {
						for (pugi::xml_node value : child.children()) {
							const sgf::String valueTag = value.name();
							if (valueTag == "x") state.x = value.text().as_float();
							else if (valueTag == "y") state.y = value.text().as_float();
							else if (valueTag == "sx") state.sx = value.text().as_float();
							else if (valueTag == "sy") state.sy = value.text().as_float();
							else if (valueTag == "kx") state.kx = value.text().as_float();
							else if (valueTag == "ky") state.ky = value.text().as_float();
							else if (valueTag == "a") state.a = value.text().as_float();
							else if (valueTag == "f") state.f = value.text().as_int();
							else if (valueTag == "i") state.i = value.text().as_string();
							else if (valueTag == "font") state.font = value.text().as_string();
							else if (valueTag == "text") state.text = value.text().as_string();
						}
						track.mFrames.push_back(state);
						if (!state.i.empty())
							parsed.images.insert(state.i);
					}
				}
				if (track.mFrames.empty()) {
					SetError(error, "XML Reanimation contains a track with no frames");
					return false;
				}
				parsed.tracks.push_back(std::move(track));
			}
		}

		if (!hasFps || !std::isfinite(parsed.fps) || parsed.fps <= 0.0f) {
			SetError(error, "XML Reanimation has no valid fps");
			return false;
		}
		if (parsed.tracks.empty()) {
			SetError(error, "XML Reanimation contains no tracks");
			return false;
		}
		const std::size_t frameCount = parsed.tracks[0].mFrames.size();
		for (std::size_t index = 1; index < parsed.tracks.size(); ++index) {
			if (parsed.tracks[index].mFrames.size() != frameCount) {
				SetError(error, "XML Reanimation tracks have inconsistent frame counts");
				return false;
			}
		}
		return true;
	}

	bool HasCompiledExtension(const char* filePath)
	{
		const sgf::String path = filePath ? filePath : "";
		const sgf::String suffix = ".reanim.compiled";
		if (path.size() < suffix.size())
			return false;
		return std::equal(suffix.rbegin(), suffix.rend(), path.rbegin(),
			[](char left, char right) {
				if (left >= 'A' && left <= 'Z') left = static_cast<char>(left - 'A' + 'a');
				if (right >= 'A' && right <= 'Z') right = static_cast<char>(right - 'A' + 'a');
				return left == right;
			});
	}
}

sgf::Reanimation::Reanimation()
{
	mTracks = std::make_shared<std::vector<TrackInfo>>(std::vector<TrackInfo>());
	mImagesSet = std::make_shared<std::set<sgf::String>>(std::set<sgf::String>());
	mResourceManager = nullptr;
}

sgf::Reanimation::~Reanimation()
{

}

bool sgf::Reanimation::LoadFromFile(const char* filePath)
{
	mIsLoaded = false;
	mLastError.clear();

	std::vector<unsigned char> source;
	if (!ReadFileBytes(filePath, source, mLastError)) {
		std::cerr << "Loading " << (filePath ? filePath : "") << " failed: " << mLastError << std::endl;
		return false;
	}

	ParsedReanimation parsed;
	bool loaded = false;
	const bool hasBinaryMagic = source.size() >= 4 &&
		(ReadLittleEndianU32(&source[0]) == kCompiledMagic ||
			ReadLittleEndianU32(&source[0]) == kRawMagic);
	if (hasBinaryMagic || HasCompiledExtension(filePath)) {
		std::vector<unsigned char> raw;
		if (source.size() >= 4 && ReadLittleEndianU32(&source[0]) == kRawMagic)
			raw = source;
		else if (!InflateCompiled(source, raw, mLastError)) {
			std::cerr << "Loading " << (filePath ? filePath : "") << " failed: " << mLastError << std::endl;
			return false;
		}
		loaded = ParseCompiledRaw(raw, parsed, mLastError);
	}
	else {
		loaded = ParseXml(source, parsed, mLastError);
	}

	if (!loaded) {
		std::cerr << "Loading " << (filePath ? filePath : "") << " failed: " << mLastError << std::endl;
		return false;
	}

	mFPS = parsed.fps;
	mTracks = std::make_shared<std::vector<TrackInfo>>(std::move(parsed.tracks));
	mImagesSet = std::make_shared<std::set<String>>(std::move(parsed.images));
	mIsLoaded = true;
	return true;
}




void sgf::Reanimation::Present(Graphics* g, int frameIndex)
{
	Reanimation* R = this;
	int OffsetX = 0;
	int OffsetY = 0;
	float fScale = 1.0f;

	for (size_t i = 0; i < R->mTracks->size(); i++) {
		auto& x = (R->mTracks)->at(i);
		if (!x.mAvailable)
			continue;
		TrackFrameTransform aSource = x.mFrames[frameIndex];
		if (aSource.i != "" && !aSource.f) {
			static glm::mat4x4 animMatrix = glm::mat4x4(1.0f);
			Point graPos = g->GetGraphicsTransformPosition();
			TransformToMatrixEx(aSource, animMatrix, fScale, fScale, graPos.x, graPos.y);

			SimpleImage* targetImage = (SimpleImage*)mResourceManager->mResourcePool[aSource.i];
			if (targetImage) {
				//g->DrawImageMatrixFixed(targetImage,animMatrix);

				float matrixPosition = g->TryToBindNewMatrix(animMatrix);
				int tex = g->TryToBindNewTexture(targetImage);

				for (size_t i = 0; i < 6; i++)
				{
					g->mCubeVertices[i].matrixIndex = matrixPosition;
					g->mCubeVertices[i].texIndex = tex;
				}

				g->mCubeVertices[0].x = targetImage->mSurface->w;
				g->mCubeVertices[0].y = targetImage->mSurface->h;
				g->mCubeVertices[1].x = targetImage->mSurface->w;

				g->mCubeVertices[3].x = targetImage->mSurface->w;
				g->mCubeVertices[3].y = targetImage->mSurface->h;
				g->mCubeVertices[4].y = targetImage->mSurface->h;

				g->AppendVertices(g->mCubeVertices, 6);

				g->CheckSubmit();

				for (size_t i = 0; i < 6; i++)
				{
					g->mCubeVertices[i].texIndex = -1;
					g->mCubeVertices[i].matrixIndex = -1;
				}

			}
		}

	}
}

void sgf::TransformToMatrixEx(
	sgf::TrackFrameTransform& src, glm::mat4x4& dest, float ScaleX, float ScaleY, float pX, float pY)
{
	float aSkewX = (src.kx) * -0.017453292f;
	float aSkewY = (src.ky) * -0.017453292f;


	sgf::GameMat44* destg = (sgf::GameMat44*)&dest;
	destg->x1 = cos(aSkewX);
	destg->x2 = -sin(aSkewX);

	destg->y1 = sin(aSkewY);
	destg->y2 = cos(aSkewY);

	destg->t1 = src.x * ScaleX + pX;
	destg->t2 = src.y * ScaleY + pY;
	destg->t3 = 1.0f;

	dest = glm::scale(dest, glm::vec3(src.sx * ScaleX, src.sy * ScaleY, 1.0f));
}
