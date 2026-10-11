#pragma once
#include <array>
#include <atomic>

namespace torden::dsp
{
    // Ring buffer of the output (left and right), written by the audio thread and read by the displays.
    struct ScopeBuffer
    {
        static constexpr int size = 32768;
        std::array<float, size> left {}, right {};
        std::atomic<int> writePos { 0 };
        std::atomic<double> sampleRate { 48000.0 };

        void push (const float* l, const float* r, int n)
        {
            int w = writePos.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i)
            {
                left[(size_t) w] = l[i];
                right[(size_t) w] = r[i];
                w = (w + 1) & (size - 1);
            }
            writePos.store (w, std::memory_order_release);
        }

        // Copies the newest n samples (mono sum when r == nullptr)
        void copyLatest (float* l, float* r, int n) const
        {
            const int end = writePos.load (std::memory_order_acquire);
            for (int i = 0; i < n; ++i)
            {
                const auto idx = (size_t) ((end - n + i + size * 4) & (size - 1));
                if (r == nullptr) l[i] = 0.5f * (left[idx] + right[idx]);
                else { l[i] = left[idx]; r[i] = right[idx]; }
            }
        }
    };
}
