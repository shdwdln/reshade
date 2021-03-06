#include "EffectPreprocessor.hpp"

#include <fpp.h>
#include <array>
#include <boost\algorithm\string\trim.hpp>

namespace ReShade
{
	struct EffectPreprocessor::Impl
	{
		static void OnOutput(EffectPreprocessor *pp, char ch)
		{
			if (pp->mImpl->mLastPragma != std::string::npos)
			{
				if (ch == '\n')
				{
					std::string pragma = pp->mImpl->mOutput.substr(pp->mImpl->mLastPragma);
					boost::algorithm::trim(pragma);

					pp->mPragmas.push_back(pragma);
					pp->mImpl->mLastPragma = std::string::npos;
				}
			}
			else
			{
				const std::size_t length = pp->mImpl->mOutput.size();

				if (length > 7 && pp->mImpl->mOutput.substr(length - 7) == "#pragma")
				{
					pp->mImpl->mLastPragma = length;
				}
			}

			pp->mImpl->mOutput += ch;
		}
		static void OnPrintError(EffectPreprocessor *pp, const char *format, va_list args)
		{
			char buffer[1024];
			vsprintf_s(buffer, format, args);

			pp->mImpl->mErrors += buffer;
		}

		std::vector<fppTag> mTags;
		std::string mOutput, mErrors;
		std::size_t mScratchCursor, mLastPragma;
		std::array<char, 16384> mScratch;
	};

	// -----------------------------------------------------------------------------------------------------

	EffectPreprocessor::EffectPreprocessor() : mImpl(new Impl())
	{
		this->mImpl->mScratchCursor = 0;
		this->mImpl->mLastPragma = std::string::npos;

		this->mImpl->mTags.resize(7);
		this->mImpl->mTags[0].tag = FPPTAG_USERDATA;
		this->mImpl->mTags[0].data = static_cast<void *>(this);
		this->mImpl->mTags[1].tag = FPPTAG_OUTPUT;
		this->mImpl->mTags[1].data = reinterpret_cast<void *>(&Impl::OnOutput);
		this->mImpl->mTags[2].tag = FPPTAG_ERROR;
		this->mImpl->mTags[2].data = reinterpret_cast<void *>(&Impl::OnPrintError);
		this->mImpl->mTags[3].tag = FPPTAG_IGNOREVERSION;
		this->mImpl->mTags[3].data = reinterpret_cast<void *>(true);
		this->mImpl->mTags[4].tag = FPPTAG_OUTPUTLINE;
		this->mImpl->mTags[4].data = reinterpret_cast<void *>(true);
		this->mImpl->mTags[5].tag = FPPTAG_OUTPUTSPACE;
		this->mImpl->mTags[5].data = reinterpret_cast<void *>(true);
		this->mImpl->mTags[6].tag = FPPTAG_OUTPUTINCLUDES;
		this->mImpl->mTags[6].data = reinterpret_cast<void *>(true);
	}
	EffectPreprocessor::~EffectPreprocessor()
	{
	}

	void EffectPreprocessor::AddDefine(const std::string &name, const std::string &value)
	{
		const std::string define = name + (value.empty() ? "" : "=" + value);
		const std::size_t size = define.length() + 1;

		assert(this->mImpl->mScratchCursor + size < this->mImpl->mScratch.size());

		fppTag tag;
		tag.tag = FPPTAG_DEFINE;
		tag.data = std::memcpy(this->mImpl->mScratch.data() + this->mImpl->mScratchCursor, define.c_str(), size);
		this->mImpl->mTags.push_back(tag);
		this->mImpl->mScratchCursor += size;
	}
	void EffectPreprocessor::AddIncludePath(const boost::filesystem::path &path)
	{
		const std::string directory = path.string() + '\\';
		const std::size_t size = directory.length() + 1;

		assert(this->mImpl->mScratchCursor + size < this->mImpl->mScratch.size());

		fppTag tag;
		tag.tag = FPPTAG_INCLUDE_DIR;
		tag.data = std::memcpy(this->mImpl->mScratch.data() + this->mImpl->mScratchCursor, directory.c_str(), size);
		this->mImpl->mTags.push_back(tag);
		this->mImpl->mScratchCursor += size;
	}

	std::string EffectPreprocessor::Run(const boost::filesystem::path &path, std::string &errors)
	{
		this->mImpl->mOutput.clear();
		this->mImpl->mErrors.clear();

		fppTag tag;
		std::vector<fppTag> tags = this->mImpl->mTags;
		const std::string name = path.string();

		tag.tag = FPPTAG_INPUT_NAME;
		tag.data = const_cast<void *>(static_cast<const void *>(name.c_str()));
		tags.push_back(tag);

		tag.tag = FPPTAG_END;
		tag.data = nullptr;
		tags.push_back(tag);

		// Run preprocessor
		const bool success = fppPreProcess(tags.data()) == 0;

		// Add included files
		std::size_t pos = 0;

		while ((pos = this->mImpl->mErrors.find("Included", pos)) != std::string::npos)
		{
			const std::size_t begin = this->mImpl->mErrors.find_first_of('"', pos) + 1, end = this->mImpl->mErrors.find_first_of('"', begin);
			const std::string include = this->mImpl->mErrors.substr(begin, end - begin);

			this->mImpl->mErrors.erase(pos, 12 + end - begin);
			this->mIncludes.push_back(include);
		}

		std::sort(this->mIncludes.begin(), this->mIncludes.end());
		this->mIncludes.erase(std::unique(this->mIncludes.begin(), this->mIncludes.end()), this->mIncludes.end());

		// Return preprocessed source
		if (!success)
		{
			errors += this->mImpl->mErrors;

			this->mImpl->mOutput.clear();
		}

		return this->mImpl->mOutput;
	}
}