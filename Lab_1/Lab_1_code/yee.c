// This thread handkes playback
static PT_THREAD (protothread_playback(struct pt *pt))
{
    // Indicate thread beginning
    PT_BEGIN(pt) ;

    while (1){

      if (play){

        if (compose_length == 0) {
            compose_length = 1
            compose_sequence = {play_button}
        }

        for (int j = 0; j < compose_length; j++) {
            if (play_index < sound_length[(compose_sequence[j]) - 1]) {
                uint16_t frequency = sounds[compose_sequence[j] - 1][play_index++];

                phase_incr_main = (unsigned int)((frequency * two32) / Fs);
            }
            else {
                play_index = 0;
                play = false;
                tone_enabled = false;
            }
        }
      }
      else{
        play_index = 0;
      }

      PT_YIELD_usec(1000) ;
    }

    PT_END(pt) ;

}