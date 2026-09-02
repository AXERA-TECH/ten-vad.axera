//
// Copyright © 2025 Agora
// This file is part of TEN Framework, an open source project.
// Licensed under the Apache License, Version 2.0, with certain conditions.
// Refer to the "LICENSE" file in the root directory for more information.
//
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <string.h> // memcmp
#include "ten_vad.h"

const int hop_size = 256; // 16 ms per frame

uint64_t get_timestamp_ms()
{
  struct timespec ts;
  uint64_t millis;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  millis = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  return millis;
}

// define RIFF header
#pragma pack(push, 1)
typedef struct
{
  char chunk_id[4];    // should be "RIFF"
  uint32_t chunk_size; // file total size - 8
  char format[4];      // should be "WAVE"
} riff_header_t;

// define each sub chunk header
typedef struct
{
  char id[4];    // should be "fmt " or "data"
  uint32_t size; // chunk data size
} chunk_header_t;
#pragma pack(pop)

// define WAV file info we care about
typedef struct
{
  uint16_t audio_format;    // audio format (e.g. PCM=1)
  uint16_t num_channels;    // number of channels
  uint32_t sample_rate;     // sample rate
  uint32_t byte_rate;       // byte rate
  uint16_t block_align;     // block align
  uint16_t bits_per_sample; // bits per sample
  uint32_t data_size;       // data size
  long data_offset;         // data offset in file
} wav_info_t;

int read_wav_file(FILE *fp, wav_info_t *info);

int write_stereo_wav(const char *path, const int16_t *audio,
                     uint32_t sample_num, const int32_t *flags)
{
  FILE *fp = fopen(path, "wb");
  if (fp == NULL)
    return -1;

  const uint16_t channels = 2;
  const uint16_t bits_per_sample = 16;
  const uint32_t sample_rate = 16000;
  const uint16_t block_align = channels * bits_per_sample / 8;
  const uint32_t byte_rate = sample_rate * block_align;
  const uint32_t data_size = sample_num * block_align;
  const uint32_t riff_size = 36 + data_size;
  const uint32_t fmt_size = 16;
  const uint16_t pcm_format = 1;

  fwrite("RIFF", 1, 4, fp);
  fwrite(&riff_size, sizeof(riff_size), 1, fp);
  fwrite("WAVE", 1, 4, fp);
  fwrite("fmt ", 1, 4, fp);
  fwrite(&fmt_size, sizeof(fmt_size), 1, fp);
  fwrite(&pcm_format, sizeof(pcm_format), 1, fp);
  fwrite(&channels, sizeof(channels), 1, fp);
  fwrite(&sample_rate, sizeof(sample_rate), 1, fp);
  fwrite(&byte_rate, sizeof(byte_rate), 1, fp);
  fwrite(&block_align, sizeof(block_align), 1, fp);
  fwrite(&bits_per_sample, sizeof(bits_per_sample), 1, fp);
  fwrite("data", 1, 4, fp);
  fwrite(&data_size, sizeof(data_size), 1, fp);
  for (uint32_t i = 0; i < sample_num; ++i)
  {
    const int16_t left_flag = flags[i / hop_size] ? INT16_MAX : 0;
    const int16_t right_audio = audio[i];
    fwrite(&left_flag, sizeof(left_flag), 1, fp);
    fwrite(&right_audio, sizeof(right_audio), 1, fp);
  }
  fclose(fp);
  return 0;
}

int vad_process(int16_t *input_buf, uint32_t frame_num,
                float *out_probs, int32_t *out_flags,
                float *use_time)
{
  printf("tenvadsrc version: %s\n", ten_vad_get_version());
  void *ten_vad_handle = NULL;
  float voice_threshold = 0.5f;
  if (ten_vad_create(&ten_vad_handle, hop_size, voice_threshold) != 0)
  {
    fprintf(stderr, "ten_vad_create failed; check axmodel/ten-vad-ax650.axmodel and AX Engine runtime\n");
    return -1;
  }

  uint64_t start = get_timestamp_ms();
  for (uint32_t i = 0; i < frame_num; ++i)
  {
    int16_t *audio_data = input_buf + i * hop_size;
    int res = ten_vad_process(ten_vad_handle, audio_data, hop_size,
                              &out_probs[i], &out_flags[i]);
    if (res == 0)
    {
      printf("[%" PRIu32 "] %0.6f, %d\n", i, out_probs[i], out_flags[i]);
    }
    else
    {
      printf("ten_vad_process failed res %d\n", res);
    }
  }
  uint64_t end = get_timestamp_ms();
  *use_time = (float)(end - start);

  ten_vad_destroy(&ten_vad_handle);
  ten_vad_handle = NULL;
  return 0;
}

int test_with_wav(int argc, char *argv[])
{
  if (argc < 3)
  {
    printf("Usage: ten_vad_example input.wav output-stereo.wav\n");
    return 0;
  }
  char *input_file = argv[1];
  char *out_file = argv[2];

  FILE *fp = fopen(input_file, "rb");
  if (fp == NULL)
  {
    printf("Failed to open input file: %s\n", input_file);
    return 1;
  }
  fseek(fp, 0, SEEK_SET);
  wav_info_t info;
  if (read_wav_file(fp, &info) != 0)
  {
    printf("Failed to read WAV file header\n");
    fclose(fp);
    return 1;
  }

  if (info.audio_format != 1 || info.num_channels != 1 ||
      info.sample_rate != 16000 || info.bits_per_sample != 16)
  {
    fprintf(stderr,
            "Unsupported WAV: require PCM16, mono, 16000 Hz; got format=%u, channels=%u, rate=%u, bits=%u\n",
            info.audio_format, info.num_channels, info.sample_rate,
            info.bits_per_sample);
    fclose(fp);
    return 1;
  }

  uint32_t byte_num = info.data_size;
  printf("WAV file byte num: %d\n", byte_num);
  uint32_t sample_num = byte_num / sizeof(int16_t);
  uint32_t padded_sample_num =
      ((sample_num + hop_size - 1) / hop_size) * hop_size;
  char *input_buf = (char *)calloc(padded_sample_num, sizeof(int16_t));
  if (input_buf == NULL)
  {
    fclose(fp);
    return 1;
  }
  fseek(fp, info.data_offset, SEEK_SET);
  if (fread(input_buf, 1, byte_num, fp) != byte_num)
  {
    fprintf(stderr, "Failed to read WAV sample data\n");
    fclose(fp);
    free(input_buf);
    return 1;
  }
  fclose(fp);
  fp = NULL;

  float total_audio_time = (float)sample_num / 16.0;
  printf("total_audio_time: %.2f(ms)\n", total_audio_time);
  uint32_t frame_num = padded_sample_num / hop_size;
  printf("Audio frame Num: %d\n", frame_num);
  float *out_probs = (float *)malloc(frame_num * sizeof(float));
  int32_t *out_flags = (int32_t *)malloc(frame_num * sizeof(int32_t));  // Output flags are binary speech indicators (0 for non-speech signal, 1 for speech signal)
  float use_time = .0;
  if (out_probs == NULL || out_flags == NULL ||
      vad_process((int16_t *)input_buf, frame_num,
                  out_probs, out_flags, &use_time) != 0)
  {
    free(input_buf);
    free(out_probs);
    free(out_flags);
    return 1;
  }
  float rtf = use_time / total_audio_time;
  printf("Consuming time: %f(ms), audio-time: %.2f(ms), =====> RTF: %0.6f\n",
          use_time, total_audio_time, rtf);

  if (write_stereo_wav(out_file, (int16_t *)input_buf, sample_num,
                       out_flags) != 0)
  {
    fprintf(stderr, "Failed to write stereo output: %s\n", out_file);
    return 1;
  }
  printf("Stereo output saved: %s (L=VAD flag, R=original audio)\n", out_file);

  free(input_buf);
  free(out_probs);
  free(out_flags);
  return 0;
}

int main(int argc, char *argv[])
{
  return test_with_wav(argc, argv);
}

// function to read WAV file info
int read_wav_file(FILE *fp, wav_info_t *info)
{
  if (fp == NULL || info == NULL)
    return -1;
  // save current file position
  long orig_pos = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  // read RIFF header
  riff_header_t riff;
  if (fread(&riff, sizeof(riff_header_t), 1, fp) != 1)
  {
    fprintf(stderr, "Can not read RIFF head\n");
    fseek(fp, orig_pos, SEEK_SET);
    return -1;
  }
  // verify RIFF/WAVE format
  if (memcmp(riff.chunk_id, "RIFF", 4) != 0 ||
      memcmp(riff.format, "WAVE", 4) != 0)
  {
    fprintf(stderr, "not a valid RIFF/WAVE file\n");
    fseek(fp, orig_pos, SEEK_SET);
    return -1;
  }
  // initialize, mark chunks not found yet
  int fmt_found = 0, data_found = 0;
  memset(info, 0, sizeof(wav_info_t));

  // iterate all chunks
  while (!feof(fp))
  {
    chunk_header_t chunk;
    if (fread(&chunk, sizeof(chunk_header_t), 1, fp) != 1)
    {
      break; // read failed, maybe end of file
    }
    // check if it's fmt chunk
    if (memcmp(chunk.id, "fmt ", 4) == 0)
    {
      // read fmt data
      fmt_found = 1;
      if (chunk.size < 16)
      {
        fprintf(stderr, "fmt chunk size is abnormal\n");
        fseek(fp, orig_pos, SEEK_SET);
        return -1;
      }
      // read fmt parameters
      if (fread(&info->audio_format, 2, 1, fp) != 1 ||
          fread(&info->num_channels, 2, 1, fp) != 1 ||
          fread(&info->sample_rate, 4, 1, fp) != 1 ||
          fread(&info->byte_rate, 4, 1, fp) != 1 ||
          fread(&info->block_align, 2, 1, fp) != 1 ||
          fread(&info->bits_per_sample, 2, 1, fp) != 1)
      {
        fprintf(stderr, "failed to read fmt data\n");
        fseek(fp, orig_pos, SEEK_SET);
        return -1;
      }
      // skip fmt extension data
      if (chunk.size > 16)
      {
        fseek(fp, chunk.size - 16, SEEK_CUR);
      }
    }
    // check if it's data chunk
    else if (memcmp(chunk.id, "data", 4) == 0)
    {
      data_found = 1;
      info->data_size = chunk.size;
      info->data_offset = ftell(fp); // record data start position
      break;                         // found data chunk, can exit loop
    }
    // other chunks, skip
    else
    {
      // consider byte alignment, pad odd size
      fseek(fp, (chunk.size + (chunk.size % 2)), SEEK_CUR);
    }
  }
  // check if necessary chunks are found
  if (!fmt_found)
  {
    fprintf(stderr, "fmt chunk not found\n");
    fseek(fp, orig_pos, SEEK_SET);
    return -1;
  }
  if (!data_found)
  {
    fprintf(stderr, "data chunk not found\n");
    fseek(fp, orig_pos, SEEK_SET);
    return -1;
  }
  // restore original file position
  fseek(fp, orig_pos, SEEK_SET);
  return 0;
}
