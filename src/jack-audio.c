// AMY live implemented directly in the JACK API.
//
// JACK ports: AMY:out{n} for n in 0..AMY_NCHANS
//
// When amy-module is linked against this instead of libminiaudio-audio.c, it
// will behave differently from the perspective of JACK.
//
// In contrast to miniaudio, which assumes its outputs should be connected to
// the first two inputs it finds and does so, this implementation creates a
// JACK process graph nodes with AMY_NCHANS outputs and does not connect them
// to anything, on the assumption the user will connect using qjackctl or
// whatever they prefer.
//
// Audio input to AMY is not implemented.
//
// Dynamic updates to period size are supported.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jack/jack.h>

#include "amy.h"

#define AMY_BLOCK_BYTES (AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t))

// These are part of the amy_ API but ignored for direct JACK.
int16_t amy_channel = -1;
int16_t amy_playback_device_id = -1;
int16_t amy_capture_device_id = -1;
uint8_t amy_running = 0;

static jack_client_t *client = NULL;
static jack_port_t *ports[AMY_NCHANS];
static int16_t *rendered = NULL;
static size_t rendered_pos = AMY_BLOCK_SIZE;

int process(jack_nframes_t frames_requested, void *arg) {
  jack_default_audio_sample_t *outputs[AMY_NCHANS];
  for (size_t channel = 0; channel < AMY_NCHANS; ++channel) {
    outputs[channel] = jack_port_get_buffer(ports[channel], frames_requested);
  }
  for (size_t frames_done = 0; frames_done < frames_requested;) {
    if (rendered_pos < AMY_BLOCK_SIZE) {
      for (size_t channel = 0; channel < AMY_NCHANS; ++channel) {
        jack_default_audio_sample_t sample =
            rendered[rendered_pos * AMY_NCHANS + channel];
        outputs[channel][frames_done] = sample / INT16_MAX;
      }
      ++frames_done;
      ++rendered_pos;
    } else {
      rendered = amy_simple_fill_buffer();
      rendered_pos = 0;
    }
  }
  return 0;
}

void amy_live_start() {
  if (client == NULL) {
    client = jack_client_open("AMY", JackNullOption, NULL);
    memset(ports, 0, sizeof(ports));
  }
  if (client == NULL) {
    fprintf(stderr, "jack_client_open failed\n");
    return;
  }
  jack_nframes_t sample_rate = jack_get_sample_rate(client);
  if (sample_rate != AMY_SAMPLE_RATE) {
    fprintf(stderr,
            "sample rates must match but have %d for JACK vs. %d for AMY\n",
            sample_rate, AMY_SAMPLE_RATE);
  }
  for (size_t channel = 0; channel < AMY_NCHANS; ++channel) {
    char name[8];
    snprintf(name, sizeof(name), "out%ld", channel);
    ports[channel] = jack_port_register(client, name, JACK_DEFAULT_AUDIO_TYPE,
                                        JackPortIsOutput, 0);
  }
  jack_set_process_callback(client, process, 0);
  jack_activate(client);
}

void amy_live_stop() {
  if (client != NULL) {
    jack_deactivate(client);
    for (size_t channel = 0; channel < AMY_NCHANS; ++channel) {
      if (ports[channel] != NULL) {
        jack_port_unregister(client, ports[channel]);
        ports[channel] = NULL;
      }
    }
    jack_client_close(client);
    client = NULL;
  }
}
