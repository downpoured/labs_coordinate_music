#include "wavcut.h"
#include <algorithm>

std::string getOutputFileName(const char* prefix, int n)
{
	std::string ret = prefix;
	ret += "out_";
	ret += StringNumberHolder(n, 3);
	ret += ".wav";
	return ret;
}

errormsg startOutputFile(FileWriteWrapper** f, int whichOutputFile, const char* prefix, const WavFileInfoT* info, int64* nLengthOfHeader)
{
	assertTrue(*f == null);
	std::string outputname = getOutputFileName(prefix, whichOutputFile);
	if (fileExists(outputname.c_str()))
		return_err("file already exists");

	FILE* ftemp = fopen(outputname.c_str(), "wb");
	if (!ftemp)
		return_err("could not create output file");

	errormsg msg = writeWavHeader(ftemp, 1 /*length in samples, to be corrected afterwards*/, info->nBitsPerSample, info->nSampleRate);
	if (msg)
		return msg;

	*nLengthOfHeader = ::_ftelli64(ftemp);
	fclose(ftemp);
	*f = new FileWriteWrapper(outputname.c_str(), "rb+");
	int seekres = (*f)->fseek64(*nLengthOfHeader, SEEK_SET);
	if (seekres != 0)
		return_err("seek failed");

	return OK;
}

errormsg finishOutputFileAndStartNext(FileWriteWrapper** f, int whichOutputFile, const char* prefix, const WavFileInfoT* info, int64 lengthOfHeader, bool fStartNext, bool isMark)
{
	// which sample are we at?
	int64 whichByte = (*f)->ftell64() - lengthOfHeader;
	assertTrue(whichByte % 4 == 0);
	if (whichByte >= fourgb)
	{
		return_err("Error: track is bigger than 4GB, too big.");
	}

	// truncate the ramp we don't want.
	int32 lengthInSamples = (int32)(whichByte / 4);
	if (isMark)
	{
		lengthInSamples = std::max(2, lengthInSamples - 133400);
	}
	printf("\n%d) Writing a track with length ", whichOutputFile - 1);
	printADuration(lengthInSamples, info->nSampleRate);

	// first go back and fix the header with the correct length
	FILE* ftemp = (*f)->detach();
	*f = null;
	assertTrue(ftemp != null);
	if (_fseeki64(ftemp, 0, SEEK_SET) != 0)
		return_err("seek failed");

	// write the corrected header
	errormsg msg = writeWavHeader(ftemp, lengthInSamples, info->nBitsPerSample, info->nSampleRate);
	fclose(ftemp);
	ftemp = null;
	if (msg)
		return msg;

	// then go and truncate what we didn't want.
	std::string filename = getOutputFileName(prefix, whichOutputFile - 1);
	errormsg err = truncateFile(filename.c_str(), lengthOfHeader + 4 * lengthInSamples);
	if (err)
		printf("\nNon-fatal Warning: truncate failed with:\n%s\n.", err);

	int64 lenHeader = 0;
	if (fStartNext)
		return startOutputFile(f, whichOutputFile, prefix, info, &lenHeader);
	else
		return OK;
}

void debugWriteTimesToDisk(const char* filename, int64* times, int timesLen)
{
#ifdef _DEBUG
	FILE* fDebug = fopen(filename, "w");
	double pos = 0;
	for (int i = 0; i < timesLen; i++)
	{
		pos += times[i] / 44100.0;
		fprintf(fDebug, "%f\n", pos);
	} fclose(fDebug);
#endif
}

errormsg adjustGivenTimesBasedOnObservedSilence(FILE* f, const WavFileInfoT* info, int64* times, int timesLen, int64 startingpoint, SimpleBuffer& trackNeedsFadeIn, SimpleBuffer& trackNeedsFadeOut)
{
	const uint64 bytesPerSample = 4;
	SimpleBuffer bufferWindow(4 * bytesPerSample * 44100);
	
	for (int i = 0; i < timesLen; i++)
	{
		if (times[i] / 44100.0 <= 9)
			return_err("we don't support tracks shorter than 9seconds");

		// go to the approximate end of the song. take a 4 second window and look for longest silence.
		int lookBefore = 2;
		int64 timeprev = i == 0 ? 0 : times[i - 1];
		if (startingpoint + (times[i] + timeprev)*bytesPerSample - lookBefore * 44100 * bytesPerSample >= info->nExpectedSamples * bytesPerSample ||
			startingpoint + (times[i] + timeprev)*bytesPerSample - lookBefore * 44100 * bytesPerSample + bufferWindow.Size() >= info->nExpectedSamples * bytesPerSample)
			return_err("tried to read past end of file??");
		int64 positionToSeekToNotCountingStartingPoint = (times[i] + timeprev)*bytesPerSample - lookBefore * 44100 * bytesPerSample;
		int seekres = _fseeki64(f, startingpoint + positionToSeekToNotCountingStartingPoint, SEEK_SET);
		if (seekres != 0) return_err("seek failed");

		// read from disk
		size_t amountRead = fread(bufferWindow.Get(), sizeof(byte), bufferWindow.Size(), f);
		if (amountRead != bufferWindow.Size())
			return_err("did not read full buffer");

		// look for silence in the next 4 seconds
		int longestPeriodOfSilenceStart = 0, longestPeriodOfSilenceLength = 0, currentStart = 0, currentElapsedSilence = 0;
		bool isSilentPrev = false;
		byte* buffer = bufferWindow.Get();
		for (size_t j = 0; j < bufferWindow.Size() / 4; j++)
		{
			int b1 = buffer[j * 4 + 2], b2 = buffer[j * 4 + 3];
			short sh1 = (short)b1; // intel byte order
			short sh2 = (short)(((short)b2) << 8);
			short shVal = (short)(sh1 + sh2);
			bool isSilent = shVal < 9 && shVal > -9;
			if (isSilent && !isSilentPrev)
			{
				currentStart = j;
				currentElapsedSilence = 0;
			}
			else if (isSilent && isSilentPrev)
			{
				currentElapsedSilence++;
			}
			else if (!isSilent && !isSilentPrev)
			{
				currentElapsedSilence = 0;
			}
			else if (!isSilent && isSilentPrev)
			{
				if (currentElapsedSilence > longestPeriodOfSilenceLength)
				{
					longestPeriodOfSilenceStart = currentStart;
					longestPeriodOfSilenceLength = currentElapsedSilence;
				}
				currentStart = 0;
				currentElapsedSilence = 0;
			}
			isSilentPrev = isSilent;
		}

		// check for the case where the buffer ends with a period of silence
		if (currentElapsedSilence > longestPeriodOfSilenceLength)
		{
			longestPeriodOfSilenceStart = currentStart;
			longestPeriodOfSilenceLength = currentElapsedSilence;
		}

		if (longestPeriodOfSilenceLength <= 50)
		{
			printf("\ndid not find very much silence in track %d, only %d. Will apply auto-fade", i + 1, longestPeriodOfSilenceLength);
			trackNeedsFadeIn.Get()[i] = 1;
			if (i>0)
				trackNeedsFadeOut.Get()[i - 1] = 1;
		}

		// return the best spot (has the longest silence).
		int64 relativespot = longestPeriodOfSilenceStart + longestPeriodOfSilenceLength - std::min(longestPeriodOfSilenceLength, 1400);
		int64 nextBytePos = positionToSeekToNotCountingStartingPoint + relativespot*bytesPerSample; // relativespot = longestPeriodOfSilenceStart + longestPeriodOfSilenceLength / 2;
		times[i] = nextBytePos / bytesPerSample;
	}

	return OK;
}


errormsg runWavCutUsingProvidedTimes(FILE* f, const WavFileInfoT* info, const char* prefix, int64* times, int timesLen)
{
	debugWriteTimesToDisk("./dataLengthUnadjusted.txt", times, timesLen);

	// in a 4 second window, use observed silence to see where the end actually is. (or if no silence, we need fade-out).
	int64 startingpoint = _ftelli64(f);
	const uint64 bytesPerSample = 4;
	SimpleBuffer trackNeedsFadeIn(timesLen);
	SimpleBuffer trackNeedsFadeOut(timesLen); 
	errormsg err = adjustGivenTimesBasedOnObservedSilence(f, info, times, timesLen, startingpoint, trackNeedsFadeIn, trackNeedsFadeOut);
	if (err)
		return err;

	debugWriteTimesToDisk("./dataLengthAdjusted.txt", times, timesLen);
	
	// write output file.
	SimpleBuffer bufferToWrite(1 * 1024 * 1024);
	for (int i = 0; i < timesLen + 1; i++)
	{
		std::string filename = getOutputFileName(prefix, i + 1);
		if (fileExists(filename.c_str()))
			return_err("file already exists");

		int64 startpos = i == 0 ? 0 : times[i - 1];
		int64 endpos = i == timesLen ? info->nExpectedSamples : times[i];
		uint64 lenInSamples = bytesPerSample*(endpos - startpos);
		if (lenInSamples >= fourgb)
			return_err("track is too big for wave format");

		FILE* fout = fopen(filename.c_str(), "wb");
		if (!fout)
			return_err("could not create output file");

		errormsg msg = writeWavHeader(fout, (int)(lenInSamples), info->nBitsPerSample, info->nSampleRate);
		if (msg)
			return msg;

		int64 startingPointOutput = _ftelli64(fout);
		int seekres = _fseeki64(f, startingpoint + startpos * bytesPerSample, SEEK_SET);
		if (seekres != 0)
			return_err("seek failed");

		uint64 written = 0;
		while (written < lenInSamples)
		{
			size_t nGotThisIteration = fread(bufferToWrite.Get(), sizeof(byte), std::min((size_t)(lenInSamples - written), bufferToWrite.Size()), f);
			if (!nGotThisIteration)
				return_err("error: read no data");

			byte* ptr = bufferToWrite.Get();
			if (i>0 && trackNeedsFadeIn.Get()[i - 1] && (written == 0))
			{
				size_t fadeLength = std::min((size_t)(.6 * 44100), nGotThisIteration / 4);
				printf("\nin file %s writing fadein", filename.c_str());
				for (size_t j = 0; j < fadeLength; j++)
				{
					double factor = j / ((double)fadeLength);
					{short sh1 = (short)ptr[j * 4]; // intel byte order
					short sh2 = (short)(((short)ptr[j * 4 + 1]) << 8);
					short shVal = (short)(sh1 + sh2);
					short shNewVal = (short)(int)(shVal*factor);
					ptr[j * 4] = shNewVal & 0xff;
					ptr[j * 4 + 1] = shNewVal >> 8; }

					{short sh1 = (short)ptr[j * 4 + 2]; // intel byte order
					short sh2 = (short)(((short)ptr[j * 4 + 3]) << 8);
					short shVal = (short)(sh1 + sh2);
					short shNewVal = (short)(int)(shVal*factor);
					ptr[j * 4 + 2] = shNewVal & 0xff;
					ptr[j * 4 + 3] = shNewVal >> 8; }
				}
			}
			else if (i>0 && trackNeedsFadeOut.Get()[i - 1] && (written + nGotThisIteration >= lenInSamples))
			{
				// fade out the last part of this buffer.
				size_t fadeLength = std::min((size_t)(.6 * 44100), nGotThisIteration / 4);
				printf("\nin file %s writing fadeout len=%d", filename.c_str(), fadeLength);
				for (size_t jcounter = 0; jcounter < fadeLength; jcounter++)
				{
					double factor = (fadeLength - jcounter) / ((double)fadeLength);
					size_t j = jcounter + nGotThisIteration / 4 - fadeLength;
					{short sh1 = (short)ptr[j * 4]; // intel byte order
					short sh2 = (short)(((short)ptr[j * 4 + 1]) << 8);
					short shVal = (short)(sh1 + sh2);
					short shNewVal = (short)(int)(shVal*factor);
					ptr[j * 4] = shNewVal & 0xff;
					ptr[j * 4 + 1] = shNewVal >> 8; }

					{short sh1 = (short)ptr[j * 4 + 2]; // intel byte order
					short sh2 = (short)(((short)ptr[j * 4 + 3]) << 8);
					short shVal = (short)(sh1 + sh2);
					short shNewVal = (short)(int)(shVal*factor);
					ptr[j * 4 + 2] = shNewVal & 0xff;
					ptr[j * 4 + 3] = shNewVal >> 8; }
				}
			}
			fwrite(bufferToWrite.Get(), nGotThisIteration, sizeof(byte), fout);
			written += nGotThisIteration;
		}

		fclose(fout);
	}

	return OK;
}

const short amplitudeThreshold = (short)(0.09*(SHRT_MAX - 1));
errormsg runWavCutUsingAudioMark(FILE* f, const WavFileInfoT* info, const char* prefix)
{
	int whichOutputFile = 1;
	FileWriteWrapper* outputFile = 0;
	int64 lengthOfHeader = 0;
	errormsg err = startOutputFile(&outputFile, whichOutputFile, prefix, info, &lengthOfHeader);
	if (err)
		return err;

	int nConsectiveWellAboveZero = 0;
	for (uint64 i = 0; i < info->nExpectedSamples; i++)
	{
		int b = 0;
		b = fgetc(f);
		outputFile->fputc(b);
		b = fgetc(f);
		outputFile->fputc(b);
		int b1 = fgetc(f);
		outputFile->fputc(b1);
		int b2 = fgetc(f);
		outputFile->fputc(b2);

		// we only need to look at one of the channels
		short sh1 = (short)b1; // intel byte order
		short sh2 = (short)(((short)b2) << 8);
		short shVal = (short)(sh1 + sh2);
		if (shVal > amplitudeThreshold)
		{
			nConsectiveWellAboveZero++;
		}
		else
		{
			if (nConsectiveWellAboveZero > 47 * 1000)
			{
				++whichOutputFile;
				err = finishOutputFileAndStartNext(&outputFile, whichOutputFile, prefix, info, lengthOfHeader, true, true /*fUseMark*/);
				if (err)
					return err;

				// seek forward past the silence
				int64 nSamplesSeekForward = 44 * 1000;
				i += nSamplesSeekForward;
				int seekres = _fseeki64(f, nSamplesSeekForward * 4/*bytes per sample*/, SEEK_CUR);
				if (seekres != 0)
					return_err("seek failed");

				nConsectiveWellAboveZero = 0;
			}
			else
			{
				nConsectiveWellAboveZero = 0;
			}
		}
	}

	++whichOutputFile;
	err = finishOutputFileAndStartNext(&outputFile, whichOutputFile, prefix, info, lengthOfHeader, false, true /*fUseMark*/);
	if (err)
		return err;

	return OK;
}


errormsg runWavCutUsingAutoDetectSilence(FILE* f, const WavFileInfoT* info, const char* prefix)
{
	int whichOutputFile = 1;
	FileWriteWrapper* outputFile = 0;
	int64 lengthOfHeader = 0;
	errormsg err = startOutputFile(&outputFile, whichOutputFile, prefix, info, &lengthOfHeader);
	if (err)
		return err;

	int64 nSamplesCloseToZero = 0;
	int64 nLastNotZero = 0;
	int64 nFileWrittenAt = 0;
	for (uint64 i = 0; i < info->nExpectedSamples; i++)
	{
		int b = 0;
		b = fgetc(f);
		outputFile->fputc(b);
		b = fgetc(f);
		outputFile->fputc(b);
		int b1 = fgetc(f);
		outputFile->fputc(b1);
		int b2 = fgetc(f);
		outputFile->fputc(b2);

		// 8 seconds is shortest song length
		if (i - nFileWrittenAt < 44100 * 8)
			continue;

		// only look at one of the channels
		short sh1 = (short)b1; // intel byte order
		short sh2 = (short)(((short)b2) << 8);
		short shVal = (short)(sh1 + sh2);
		if (shVal < 9 && shVal > -9)
		{
			nSamplesCloseToZero++;
		}
		else
		{
			if (nSamplesCloseToZero > 14550 /*0.03s*/)
			{
				double labeltime = i / 44100.0;

				// first, write the track we came up with
				++whichOutputFile;
				err = finishOutputFileAndStartNext(&outputFile, whichOutputFile, prefix, info, lengthOfHeader, true, false /*fUseMark*/);
				if (err)
					return err;

				// now, skip backwards a bit
				int64 newPoint = std::max((uint64)0, i - 13550 /*0.03s*/);
				int seekres = _fseeki64(f, (newPoint - i) * 4/*bytes per sample*/, SEEK_CUR);
				if (seekres != 0) return_err("seek failed");
				i = newPoint;
				nFileWrittenAt = i;

				labeltime = i / 44100.0;
			}
			nSamplesCloseToZero = 0;
			nLastNotZero = i;
		}
	}

	++whichOutputFile;
	err = finishOutputFileAndStartNext(&outputFile, whichOutputFile, prefix, info, lengthOfHeader, false, false /*fUseMark*/);
	if (err)
		return err;

	return OK;
}


bool checkForNameConflict(const char* prefix)
{
	for (int i = 0; i < 30; i++)
	{
		if (fileExists(getOutputFileName(prefix, i).c_str()))
			return true;
	}
	return false;
}

int runWavCut(const char* path, const char* pathLengthsFile, FILE* f)
{
	if (!stringEndsWith(path, ".wav") && !getBoolFromUser("expected a .wav file. continue?"))
		return 1;

	if (pathLengthsFile && !stringEndsWith(pathLengthsFile, ".txt") && !getBoolFromUser("expected a .txt file. continue?"))
		return 1;

	uint64 size = getFileSize(path);
	uint64 parts = size / fourgb;
	uint64 addToLength = 0;
	if (parts > 0)
	{
		printf("file has %d 4gb sections, should we read the entire file? ", (int)parts);
		if (getBoolFromUser(""))
		{
			addToLength = fourgb * parts;
		}
	}

	WavFileInfoT info;
	errormsg msg = readWavFileHeader(f, &info, addToLength);
	if (msg)
	{
		fclose(f);
		printf("\nError reading wav: %s", msg);
		return 1;
	}

	printf("\n\nLength of file is ");
	printADuration(info.nExpectedSamples, info.nSampleRate);

	int nPrefix = 'a';
	printf("\nenter a one-letter prefix for output filenames (a-z):");
	while (true)
	{
		nPrefix = getchar();
		if (nPrefix >= 'a' && nPrefix <= 'z')
			break;
	}

	char prefix[] = { (char)nPrefix, 0 };
	if (checkForNameConflict(prefix))
	{
		printf("\nLikely name conflict, please move all out_01.wav and out_02.wav files so that they aren't overwritten.");
		return 1;
	}

	PerfTimer timer;
	if (pathLengthsFile)
	{
		std::vector<int64> lengths = parseLengthsFile(pathLengthsFile, 44100);
		if (!lengths.size())
			return 1;

		msg = runWavCutUsingProvidedTimes(f, &info, prefix, &lengths[0], lengths.size());
	}
	else
	{
		bool fUseMark = getBoolFromUser(" Use audio mark (y), or look for silence (n)?");
		if (!fUseMark)
			msg = runWavCutUsingAutoDetectSilence(f, &info, prefix);
		else
			msg = runWavCutUsingAudioMark(f, &info, prefix);
	}

	if (msg)
	{
		printf("\nError cutting wav: %s", msg);
		return 1;
	}

	printf("\ncompleted in %f seconds.", timer.stop());
	printf("\nComplete.");
	return 0;
}

int runWavCut(const char* path, const char* pathLengthsFile)
{
	FILE* f = fopen(path, "rb");
	if (!f)
	{
		printf("\nCould not open this file.");
		return 1;
	}

	int ret = runWavCut(path, pathLengthsFile, f);
	fclose(f);
	return ret;
}
