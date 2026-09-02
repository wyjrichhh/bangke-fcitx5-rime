/*
 * SPDX-FileCopyrightText: 2017~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_RIMESTATE_H_
#define _FCITX_RIMESTATE_H_

#include "rimeengine.h"
#include <fcitx/inputcontextproperty.h>
#include <unordered_set>

namespace fcitx {

class RimeState : public InputContextProperty {
public:
    RimeState(RimeEngine *engine, InputContext &ic);

    virtual ~RimeState();

    void clear();
    void keyEvent(KeyEvent &event);
    bool getStatus(RimeStatus *status);
    void updatePreedit(InputContext *ic, const RimeContext &context);
    void updateUI(InputContext *ic, bool keyRelease,
                  bool clearReservedComments = true);
    void release();
    void commitPreedit(InputContext *ic);
    std::string subMode();
    std::string subModeLabel();
    void setLatinMode(bool latin);
    void selectSchema(const std::string &schemaId);
    RimeSessionId session() const { return session_; }
    void setAccentCommentIndices(std::unordered_set<int> indices);
    void setWarningCommentIndices(std::unordered_set<int> indices);
    const std::unordered_set<int> &accentCommentIndices() const;
    const std::unordered_set<int> &warningCommentIndices() const;
    void clearReservedCommentIndices();

private:
    void createSession();

    std::string lastMode_;
    RimeEngine *engine_;
    RimeSessionId session_ = 0;
    InputContext &ic_;

    std::unordered_set<int> accentCommentIndices_;
    std::unordered_set<int> warningCommentIndices_;
};
} // namespace fcitx

#endif // _FCITX_RIMESTATE_H_
