#if !defined(DEF_VID_DECODE_HPP)
#define DEF_VID_DECODE_HPP

// Decode pipeline threads.
void Vid_decode_thread(void* arg);
void Vid_read_packet_thread(void* arg);
void Vid_audio_decode_thread(void* arg);

#endif //!defined(DEF_VID_DECODE_HPP)
