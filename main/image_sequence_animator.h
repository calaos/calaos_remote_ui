/**
 * @file image_sequence_animator.h
 * @author Calaos Team
 * @brief Generic LVGL component for animating image sequences with frame-based transitions
 * @version 0.1
 * @date 2025-01-06
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once
#include "lvgl.h"
#include "lvgl_timer.h"
#include <vector>
#include <functional>
#include <memory>


/**
 * @brief Generic LVGL component for animating sequences of images
 *
 * This component provides a reusable way to animate through sequences of images
 * with configurable timing, repeat modes, and callbacks. Optimized for ESP32
 * with minimal memory overhead.
 */
class ImageSequenceAnimator
{
public:
    /**
     * @brief Configuration structure for animation settings
     */
    struct Config
    {
        std::vector<const lv_image_dsc_t*> frames;  ///< Pointers to existing image frames (no copying)
        const lv_image_dsc_t* staticImage = nullptr;  ///< Static image to show when not animating
        uint32_t frameDuration = 100;                 ///< Duration per frame in milliseconds
        int32_t repeatCount = 1;                      ///< Number of repeats (0=once, -1=infinite)
        bool autoReverse = false;                     ///< Auto reverse animation (ping-pong effect)
        bool threadSafe = true;                       ///< Use LVGL timer for thread safety
    };

    /**
     * @brief Animation state enumeration
     */
    enum class State {
        Idle,       ///< Not animating
        Playing,    ///< Animation in progress
        Paused,     ///< Animation paused
        Completed   ///< Animation finished
    };

public:
    /**
     * @brief Constructor with LVGL image object and configuration
     * @param imageObj LVGL image object to animate (must be valid throughout lifetime)
     * @param config Animation configuration
     */
    ImageSequenceAnimator(lv_obj_t* imageObj, const Config& config);

    /**
     * @brief Destructor - ensures proper cleanup
     */
    ~ImageSequenceAnimator();

    // Disable copy constructor and assignment operator
    ImageSequenceAnimator(const ImageSequenceAnimator&) = delete;
    ImageSequenceAnimator& operator=(const ImageSequenceAnimator&) = delete;

    // Enable move constructor and assignment operator
    ImageSequenceAnimator(ImageSequenceAnimator&& other) noexcept;
    ImageSequenceAnimator& operator=(ImageSequenceAnimator&& other) noexcept;

    /**
     * @brief Static factory methods for common configurations
     */
    static Config createOneShot(const std::vector<const lv_image_dsc_t*>& frames,
                               const lv_image_dsc_t* staticImage = nullptr,
                               uint32_t frameDuration = 100);

    static Config createLoop(const std::vector<const lv_image_dsc_t*>& frames,
                            uint32_t frameDuration = 100);

    static Config createPingPong(const std::vector<const lv_image_dsc_t*>& frames,
                                uint32_t frameDuration = 100);

    /**
     * @brief Animation control methods
     */
    void play();                    ///< Start or resume animation
    void pause();                   ///< Pause animation
    void stop();                    ///< Stop animation and reset to first frame
    void reset();                   ///< Reset to first frame without stopping
    void showStatic();              ///< Show static image if configured

    /**
     * @brief State query methods
     */
    State getState() const { return currentState_; }
    bool isPlaying() const { return currentState_ == State::Playing; }
    int getCurrentFrame() const { return currentFrameIndex_; }
    size_t getFrameCount() const { return config_.frames.size(); }

    /**
     * @brief Dynamic configuration methods
     */
    void setFrameDuration(uint32_t ms);
    void setFrames(const std::vector<const lv_image_dsc_t*>& frames);
    void setStaticImage(const lv_image_dsc_t* image);

    /**
     * @brief Callback registration
     */
    void onFrameChange(std::function<void(int frameIndex)> callback) { onFrameChange_ = callback; }
    void onComplete(std::function<void()> callback) { onComplete_ = callback; }

private:
    void updateFrame();
    void onTimerTick();
    void transitionToState(State newState);
    bool validateConfig() const;

private:
    lv_obj_t* imageObj_;                        ///< LVGL image object to animate
    Config config_;                             ///< Animation configuration
    std::unique_ptr<LvglTimer> animationTimer_; ///< Timer for frame updates

    // Animation state
    State currentState_ = State::Idle;
    int currentFrameIndex_ = 0;
    int currentRepeatCount_ = 0;
    bool reverseDirection_ = false;

    // Callbacks
    std::function<void(int)> onFrameChange_;
    std::function<void()> onComplete_;
};