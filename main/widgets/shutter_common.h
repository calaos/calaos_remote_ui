#pragma once

#include <string>
#include <cstdlib>
#include <cstdio>
#include "lvgl.h"

/**
 * @brief Parsed state for a shutter_smart IO
 *
 * State format from server: "action percent" (e.g., "stop 50", "up 30", "down 80")
 * Or just "percent" (e.g., "50")
 */
struct ShutterSmartState
{
    std::string action;  // "stop", "up", "down", or ""
    int percent = 0;     // 0 = fully open, 100 = fully closed
    bool isOpen = true;  // true if percent < 100
};

/**
 * @brief Parse the state string of a shutter_smart IO
 * @param stateStr State string from server (e.g., "stop 50", "up 30", "down 80", "50")
 * @return Parsed shutter smart state
 */
inline ShutterSmartState parseShutterSmartState(const std::string& stateStr)
{
    ShutterSmartState result;

    if (stateStr.empty())
        return result;

    // Find space separator
    size_t spacePos = stateStr.find(' ');

    if (spacePos != std::string::npos)
    {
        // Format: "action percent"
        result.action = stateStr.substr(0, spacePos);
        std::string percentStr = stateStr.substr(spacePos + 1);
        result.percent = std::atoi(percentStr.c_str());
    }
    else
    {
        // Could be just a number or just an action
        char* endPtr = nullptr;
        long val = std::strtol(stateStr.c_str(), &endPtr, 10);
        if (endPtr != stateStr.c_str() && *endPtr == '\0')
        {
            // It's a number
            result.percent = static_cast<int>(val);
        }
        else
        {
            // It's an action without percent
            result.action = stateStr;
        }
    }

    // Clamp percent
    if (result.percent < 0)
        result.percent = 0;
    if (result.percent > 100)
        result.percent = 100;

    result.isOpen = (result.percent < 100);

    return result;
}

/**
 * @brief Get the state text for a shutter position
 * @param percent Shutter position (0=open, 100=closed)
 * @return Human-readable state text
 */
inline std::string getShutterStateText(int percent)
{
    if (percent == 0)
        return "State: Opened";
    else if (percent > 0 && percent < 50)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d%% Opened", percent);
        return buf;
    }
    else if (percent >= 50 && percent < 100)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d%% Closed", percent);
        return buf;
    }
    else // percent == 100
        return "Closed";
}

/**
 * @brief Get the action text for a shutter action
 * @param action Action string ("stop", "up", "down", or "")
 * @return Human-readable action text
 */
inline std::string getShutterActionText(const std::string& action)
{
    if (action == "down")
        return "Closing...";
    else if (action == "up")
        return "Opening...";
    else
        return "Stopped";
}

/**
 * @brief Check if the gui_type is a smart shutter
 * @param guiType The gui_type string from the IO state
 * @return true if "shutter_smart"
 */
inline bool isShutterSmart(const std::string& guiType)
{
    return guiType == "shutter_smart";
}

/**
 * @brief Parse simple shutter state (bool) to open/closed
 * @param stateStr "true" or "false"
 * @return true if open ("true")
 */
inline bool parseShutterIsOpen(const std::string& stateStr)
{
    return (stateStr == "true");
}

/**
 * @brief Update the moving shutter part position based on percent
 *
 * Follows the QML reference:
 *   anchors.verticalCenterOffset: Math.round(shutterPos * 45dp / 100) - 45dp
 *
 * percent 0 = open (part_shutter2 shifted up by 45px, hidden by clip)
 * percent 100 = closed (part_shutter2 at center, fully visible)
 *
 * @param shutterMoving The moving part LVGL image object
 * @param percent Current shutter position (0-100)
 */
inline void updateShutterMovingPosition(lv_obj_t* shutterMoving, int percent)
{
    if (!shutterMoving)
        return;

    // Fixed travel range of 45px as in QML reference
    static constexpr int32_t kShutterTravel = 45 * 2;

    // Map percent 0->100 to offset -45->0
    int32_t offset = (percent * kShutterTravel / 100) - kShutterTravel;
    lv_obj_align(shutterMoving, LV_ALIGN_CENTER, 0, offset);
}

/**
 * @brief Result of creating the smart shutter visual
 */
struct ShutterVisualResult
{
    lv_obj_t* container = nullptr;  // Outer container (sized to frame)
    lv_obj_t* frame = nullptr;      // part_shutter image
    lv_obj_t* moving = nullptr;     // part_shutter2 image
};

/**
 * @brief Create the smart shutter animated visual (frame + moving part)
 *
 * Follows the QML reference where:
 * - The clip container with part_shutter2 is rendered BEHIND the frame
 * - The clip container is centered on the frame with a horizontal offset of 7px
 * - The frame (part_shutter) overlays everything on top
 *
 * @param parent Parent LVGL object
 * @param frameImg Image descriptor for the frame (part_shutter)
 * @param movingImg Image descriptor for the moving part (part_shutter2)
 * @param enableEventBubble If true, add LV_OBJ_FLAG_EVENT_BUBBLE to all elements
 * @return ShutterVisualResult with pointers to created objects
 */
inline ShutterVisualResult createShutterSmartVisual(lv_obj_t* parent,
                                                    const lv_image_dsc_t* frameImg,
                                                    const lv_image_dsc_t* movingImg,
                                                    bool enableEventBubble = false)
{
    ShutterVisualResult result;

    // Outer container sized to the frame image
    result.container = lv_obj_create(parent);
    lv_obj_remove_style_all(result.container);
    lv_obj_add_flag(result.container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_remove_flag(result.container, LV_OBJ_FLAG_SCROLLABLE);
    if (enableEventBubble)
        lv_obj_add_flag(result.container, LV_OBJ_FLAG_EVENT_BUBBLE);

    // We need to know the frame size first to set up the container
    // Create a temporary image to measure, then we'll build in the right z-order
    lv_obj_t* tmpFrame = lv_image_create(result.container);
    lv_image_set_src(tmpFrame, frameImg);
    lv_obj_update_layout(tmpFrame);
    int32_t frameW = lv_obj_get_width(tmpFrame);
    int32_t frameH = lv_obj_get_height(tmpFrame);
    lv_obj_delete(tmpFrame);

    lv_obj_set_size(result.container, frameW, frameH);

    // --- Z-order: clip container FIRST (behind), frame ON TOP ---

    // Clip container for the moving part, centered on frame with horizontal offset
    static constexpr int32_t kHorizontalOffset = 0;
    lv_obj_t* clipContainer = lv_obj_create(result.container);
    lv_obj_remove_style_all(clipContainer);
    lv_obj_set_size(clipContainer, frameW, frameH);
    lv_obj_align(clipContainer, LV_ALIGN_CENTER, kHorizontalOffset, 6);
    lv_obj_remove_flag(clipContainer, LV_OBJ_FLAG_SCROLLABLE);
    if (enableEventBubble)
        lv_obj_add_flag(clipContainer, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Moving part (part_shutter2) inside the clip container
    result.moving = lv_image_create(clipContainer);
    lv_image_set_src(result.moving, movingImg);
    lv_obj_align(result.moving, LV_ALIGN_CENTER, 0, 0);
    if (enableEventBubble)
        lv_obj_add_flag(result.moving, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Frame image (part_shutter) on top - created AFTER clip so it renders above
    result.frame = lv_image_create(result.container);
    lv_image_set_src(result.frame, frameImg);
    lv_obj_align(result.frame, LV_ALIGN_CENTER, 0, 0);
    if (enableEventBubble)
        lv_obj_add_flag(result.frame, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Initial position: fully open (moved up, hidden by clip)
    updateShutterMovingPosition(result.moving, 0);

    return result;
}

/**
 * @brief Create a single styled shutter action button
 *
 * @param parent Parent container
 * @param icon Icon image descriptor
 * @param bgOnColor Background color when pressed
 * @param eventCb LVGL event callback
 * @param userData User data for the callback
 * @return The created button object
 */
inline lv_obj_t* createShutterButton(lv_obj_t* parent,
                                     const lv_image_dsc_t* icon,
                                     lv_color_t bgOnColor,
                                     lv_event_cb_t eventCb,
                                     void* userData)
{
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(btn, 4, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(btn, bgOnColor, static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_PRESSED));
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_PRESSED));

    lv_obj_t* iconImg = lv_image_create(btn);
    lv_image_set_src(iconImg, icon);
    lv_obj_add_flag(iconImg, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_add_event_cb(btn, eventCb, LV_EVENT_CLICKED, userData);

    return btn;
}
