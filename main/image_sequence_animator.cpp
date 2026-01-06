/**
 * @file image_sequence_animator.cpp
 * @author Calaos Team
 * @brief Implementation of generic LVGL image sequence animator
 * @version 0.1
 * @date 2025-01-06
 */

#include "image_sequence_animator.h"
#include "lvgl_timer.h"
#include "logging.h"

static const char* TAG = "ImageSequenceAnimator";

// Constructor
ImageSequenceAnimator::ImageSequenceAnimator(lv_obj_t* imageObj, const Config& config)
    : imageObj_(imageObj), config_(config)
{
    if (!imageObj_) {
        ESP_LOGE(TAG, "Invalid image object provided");
        return;
    }

    if (!validateConfig()) {
        ESP_LOGE(TAG, "Invalid configuration provided");
        return;
    }

    // Set initial image
    if (config_.staticImage) {
        lv_image_set_src(imageObj_, config_.staticImage);
    } else if (!config_.frames.empty()) {
        lv_image_set_src(imageObj_, config_.frames[0]);
    }

    ESP_LOGI(TAG, "Created ImageSequenceAnimator with %zu frames, %ums duration",
             config_.frames.size(), config_.frameDuration);
}

// Destructor
ImageSequenceAnimator::~ImageSequenceAnimator()
{
    stop();
    ESP_LOGD(TAG, "Destroyed ImageSequenceAnimator");
}

// Move constructor
ImageSequenceAnimator::ImageSequenceAnimator(ImageSequenceAnimator&& other) noexcept
    : imageObj_(other.imageObj_),
      config_(std::move(other.config_)),
      animationTimer_(std::move(other.animationTimer_)),
      currentState_(other.currentState_),
      currentFrameIndex_(other.currentFrameIndex_),
      currentRepeatCount_(other.currentRepeatCount_),
      reverseDirection_(other.reverseDirection_),
      onFrameChange_(std::move(other.onFrameChange_)),
      onComplete_(std::move(other.onComplete_))
{
    // Reset other object
    other.imageObj_ = nullptr;
    other.currentState_ = State::Idle;
    other.currentFrameIndex_ = 0;
    other.currentRepeatCount_ = 0;
    other.reverseDirection_ = false;
}

// Move assignment operator
ImageSequenceAnimator& ImageSequenceAnimator::operator=(ImageSequenceAnimator&& other) noexcept
{
    if (this != &other) {
        // Clean up current resources
        stop();

        // Move from other
        imageObj_ = other.imageObj_;
        config_ = std::move(other.config_);
        animationTimer_ = std::move(other.animationTimer_);
        currentState_ = other.currentState_;
        currentFrameIndex_ = other.currentFrameIndex_;
        currentRepeatCount_ = other.currentRepeatCount_;
        reverseDirection_ = other.reverseDirection_;
        onFrameChange_ = std::move(other.onFrameChange_);
        onComplete_ = std::move(other.onComplete_);

        // Reset other
        other.imageObj_ = nullptr;
        other.currentState_ = State::Idle;
        other.currentFrameIndex_ = 0;
        other.currentRepeatCount_ = 0;
        other.reverseDirection_ = false;
    }
    return *this;
}

// Static factory methods
ImageSequenceAnimator::Config ImageSequenceAnimator::createOneShot(
    const std::vector<const lv_image_dsc_t*>& frames,
    const lv_image_dsc_t* staticImage,
    uint32_t frameDuration)
{
    Config config;
    config.frames = frames;
    config.staticImage = staticImage;
    config.frameDuration = frameDuration;
    config.repeatCount = 1;
    config.autoReverse = false;
    config.threadSafe = true;
    return config;
}

ImageSequenceAnimator::Config ImageSequenceAnimator::createLoop(
    const std::vector<const lv_image_dsc_t*>& frames,
    uint32_t frameDuration)
{
    Config config;
    config.frames = frames;
    config.staticImage = nullptr;
    config.frameDuration = frameDuration;
    config.repeatCount = -1;  // Infinite loop
    config.autoReverse = false;
    config.threadSafe = true;
    return config;
}

ImageSequenceAnimator::Config ImageSequenceAnimator::createPingPong(
    const std::vector<const lv_image_dsc_t*>& frames,
    uint32_t frameDuration)
{
    Config config;
    config.frames = frames;
    config.staticImage = nullptr;
    config.frameDuration = frameDuration;
    config.repeatCount = -1;  // Infinite ping-pong
    config.autoReverse = true;
    config.threadSafe = true;
    return config;
}

// Animation control methods
void ImageSequenceAnimator::play()
{
    if (!imageObj_ || config_.frames.empty()) {
        ESP_LOGW(TAG, "Cannot play: invalid object or no frames");
        return;
    }

    if (currentState_ == State::Playing) {
        ESP_LOGD(TAG, "Already playing");
        return;
    }

    ESP_LOGI(TAG, "Starting animation with %zu frames", config_.frames.size());

    transitionToState(State::Playing);

    // Create or restart timer
    if (config_.threadSafe) {
        animationTimer_ = std::make_unique<LvglTimer>(
            [this]() { onTimerTick(); },
            config_.frameDuration
        );
        animationTimer_->start();
    } else {
        // Direct update (caller must ensure thread safety)
        onTimerTick();
    }
}

void ImageSequenceAnimator::pause()
{
    if (currentState_ != State::Playing) {
        return;
    }

    ESP_LOGD(TAG, "Pausing animation");

    if (animationTimer_) {
        animationTimer_->pause();
    }

    transitionToState(State::Paused);
}

void ImageSequenceAnimator::stop()
{
    if (currentState_ == State::Idle) {
        return;
    }

    ESP_LOGD(TAG, "Stopping animation");

    // Stop timer
    if (animationTimer_) {
        animationTimer_->destroy();
        animationTimer_.reset();
    }

    // Reset state
    currentFrameIndex_ = 0;
    currentRepeatCount_ = 0;
    reverseDirection_ = false;

    transitionToState(State::Idle);

    // Show static image or first frame
    if (config_.staticImage) {
        lv_image_set_src(imageObj_, config_.staticImage);
    } else if (!config_.frames.empty()) {
        lv_image_set_src(imageObj_, config_.frames[0]);
    }
}

void ImageSequenceAnimator::reset()
{
    currentFrameIndex_ = 0;
    currentRepeatCount_ = 0;
    reverseDirection_ = false;

    if (!config_.frames.empty()) {
        lv_image_set_src(imageObj_, config_.frames[0]);

        if (onFrameChange_) {
            onFrameChange_(0);
        }
    }
}

void ImageSequenceAnimator::showStatic()
{
    if (config_.staticImage) {
        stop();
        lv_image_set_src(imageObj_, config_.staticImage);
        ESP_LOGD(TAG, "Showing static image");
    } else {
        ESP_LOGW(TAG, "No static image configured");
    }
}

// Configuration methods
void ImageSequenceAnimator::setFrameDuration(uint32_t ms)
{
    config_.frameDuration = ms;

    // Update timer period if currently running
    if (animationTimer_ && currentState_ == State::Playing) {
        animationTimer_->setPeriod(ms);
    }
}

void ImageSequenceAnimator::setFrames(const std::vector<const lv_image_dsc_t*>& frames)
{
    bool wasPlaying = (currentState_ == State::Playing);

    // Stop current animation
    if (wasPlaying) {
        stop();
    }

    config_.frames = frames;
    reset();

    // Restart if was playing
    if (wasPlaying && validateConfig()) {
        play();
    }
}

void ImageSequenceAnimator::setStaticImage(const lv_image_dsc_t* image)
{
    config_.staticImage = image;
}

// Private methods
void ImageSequenceAnimator::updateFrame()
{
    if (!imageObj_ || config_.frames.empty() ||
        currentFrameIndex_ < 0 || currentFrameIndex_ >= static_cast<int>(config_.frames.size())) {
        return;
    }

    // Update LVGL image
    lv_image_set_src(imageObj_, config_.frames[currentFrameIndex_]);

    // Trigger callback
    if (onFrameChange_) {
        onFrameChange_(currentFrameIndex_);
    }
}

void ImageSequenceAnimator::onTimerTick()
{
    if (currentState_ != State::Playing || config_.frames.empty()) {
        return;
    }

    // Update current frame
    updateFrame();

    // Calculate next frame index
    if (config_.autoReverse) {
        // Ping-pong animation
        if (reverseDirection_) {
            currentFrameIndex_--;
            if (currentFrameIndex_ <= 0) {
                currentFrameIndex_ = 0;
                reverseDirection_ = false;
                currentRepeatCount_++;
            }
        } else {
            currentFrameIndex_++;
            if (currentFrameIndex_ >= static_cast<int>(config_.frames.size()) - 1) {
                currentFrameIndex_ = static_cast<int>(config_.frames.size()) - 1;
                reverseDirection_ = true;
            }
        }
    } else {
        // Normal forward animation
        currentFrameIndex_++;
        if (currentFrameIndex_ >= static_cast<int>(config_.frames.size())) {
            currentFrameIndex_ = 0;
            currentRepeatCount_++;
        }
    }

    // Check if animation should complete
    if (config_.repeatCount > 0 && currentRepeatCount_ >= config_.repeatCount) {
        ESP_LOGI(TAG, "Animation completed after %d repeats", currentRepeatCount_);

        // Stop timer
        if (animationTimer_) {
            animationTimer_->destroy();
            animationTimer_.reset();
        }

        transitionToState(State::Completed);

        // Trigger completion callback
        if (onComplete_) {
            onComplete_();
        }

        // Show static image if available
        if (config_.staticImage) {
            lv_image_set_src(imageObj_, config_.staticImage);
        }
    }
}

void ImageSequenceAnimator::transitionToState(State newState)
{
    if (currentState_ != newState) {
        ESP_LOGD(TAG, "State transition: %d -> %d", static_cast<int>(currentState_), static_cast<int>(newState));
        currentState_ = newState;
    }
}

bool ImageSequenceAnimator::validateConfig() const
{
    if (config_.frames.empty()) {
        ESP_LOGW(TAG, "No frames configured");
        return false;
    }

    // Validate all frame pointers
    for (size_t i = 0; i < config_.frames.size(); ++i) {
        if (!config_.frames[i]) {
            ESP_LOGW(TAG, "Invalid frame pointer at index %zu", i);
            return false;
        }
    }

    if (config_.frameDuration < 10) {
        ESP_LOGW(TAG, "Frame duration too short: %ums (minimum 10ms recommended)", config_.frameDuration);
    }

    return true;
}