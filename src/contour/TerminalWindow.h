/**
 * This file is part of the "contour" project
 *   Copyright (c) 2019-2020 Christian Parpart <christian@parpart.family>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <contour/Actions.h>
#include <contour/Config.h>
#include <contour/FileChangeWatcher.h>
#include <terminal/Metrics.h>
#include <terminal_view/TerminalView.h>
#include <terminal_view/FontConfig.h>

#include <crispy/text/FontLoader.h>

#include <QtCore/QPoint>
#include <QtCore/QTimer>
#include <QtGui/QOpenGLWindow>
#include <QtGui/QOpenGLExtraFunctions>
#include <QtGui/QVector4D>
#include <QtWidgets/QSystemTrayIcon>

#include <atomic>
#include <fstream>
#include <memory>

namespace contour {

// XXX Maybe just now a main window and maybe later just a TerminalWindow.
//
// It currently just handles one terminal inside, but ideally later it can handle
// multiple terminals in tabbed views as well tiled.
class TerminalWindow :
    public QOpenGLWindow,
    public terminal::view::TerminalView::Events,
    protected QOpenGLExtraFunctions
{
    Q_OBJECT

  public:
    TerminalWindow(config::Config _config, std::string _profileName, std::string _programPath);
    ~TerminalWindow() override;

    static QSurfaceFormat surfaceFormat();

    void initializeGL() override;
    void paintGL() override;

    void resizeEvent(QResizeEvent* _event) override;

    void keyPressEvent(QKeyEvent* _keyEvent) override;
    void wheelEvent(QWheelEvent* _wheelEvent) override;
    void mousePressEvent(QMouseEvent* _mousePressEvent) override;
    void mouseReleaseEvent(QMouseEvent* _mouseReleaseEvent) override;
    void mouseMoveEvent(QMouseEvent* _mouseMoveEvent) override;

    void focusInEvent(QFocusEvent* _event) override;
    void focusOutEvent(QFocusEvent* _event) override;

    bool event(QEvent* _event) override;

    void post(std::function<void()> _fn);

    /// Applies given profile, potentially setting/resetting terminal configuration.
    void setProfile(config::TerminalProfile _newProfile);

  public Q_SLOTS:
    void onFrameSwapped();
    void onScreenChanged(QScreen* _screen);

  private:
    terminal::view::FontConfig loadFonts(config::TerminalProfile const& _profile);
    bool executeAction(actions::Action const& _action);
    bool executeInput(terminal::MouseEvent const& event);
    void followHyperlink(terminal::HyperlinkInfo const& _hyperlink);

    bool fullscreen() const;
    void toggleFullScreen();

    bool setFontSize(unsigned _fontSize);
    std::string extractSelectionText();
    void spawnNewTerminal(std::string const& _profileName);

    void onScreenBufferChanged(terminal::ScreenBuffer::Type _type);

    float contentScale() const;

    bool enableBackgroundBlur(bool _enable);
    bool reloadConfigValues();
    bool reloadConfigValues(std::string const& _profileName);
    bool reloadConfigValues(config::Config _newConfig);
    bool reloadConfigValues(config::Config _newConfig, std::string const& _profileName);

    void onConfigReload(FileChangeWatcher::Event /*_event*/);

    void blinkingCursorUpdate();

    void setDefaultCursor();

  private:
    void bell() override;
    void notify(std::string_view const& /*_title*/, std::string_view const& /*_body*/) override;
    void setWindowTitle(std::string_view const& /*_title*/) override;
    void onSelectionComplete() override;
    void bufferChanged(terminal::ScreenBuffer::Type) override;
    void commands(terminal::CommandList const& /*_commands*/) override;
    void resizeWindow(unsigned /*_width*/, unsigned /*_height*/, bool /*_unitInPixels*/) override;
    void onClosed() override;
    void copyToClipboard(std::string_view const& _data) override;

  signals:
    void showNotification(QString const& _title, QString const& _body);

  private:
    /// Declares the screen-dirtiness-vs-rendering state.
    enum class State {
        /// No screen updates and no rendering currently in progress.
        CleanIdle,

        /// Screen updates pending and no rendering currently in progress.
        DirtyIdle,

        /// No screen updates and rendering currently in progress.
        CleanPainting,

        /// Screen updates pending and rendering currently in progress.
        DirtyPainting
    };

    /// Defines the current screen-dirtiness-vs-rendering state.
    ///
    /// This is primarily updated by two independant threads, the rendering thread and the I/O
    /// thread.
    /// The rendering thread constantly marks the rendering state CleanPainting whenever it is about
    /// to render and, depending on whether new screen changes happened, in the frameSwapped()
    /// callback either DirtyPainting and continues to rerender or CleanIdle if no changes came in
    /// since last render.
    ///
    /// The I/O thread constantly marks the state dirty whenever new data has arrived,
    /// either DirtyIdle if no painting is currently in progress, DirtyPainting otherwise.
    std::atomic<State> state_ = State::CleanIdle;

    /// Flags the screen as dirty.
    ///
    /// @returns boolean indicating whether the screen was clean before and made dirty (true), false otherwise.
    bool setScreenDirty()
    {
        for (;;)
        {
            auto state = state_.load();
            switch (state)
            {
                case State::CleanIdle:
                    if (state_.compare_exchange_strong(state, State::DirtyIdle))
                        return true;
                    break;
                case State::CleanPainting:
                    if (state_.compare_exchange_strong(state, State::DirtyPainting))
                        return true;
                    break;
                case State::DirtyIdle:
                case State::DirtyPainting:
                    return false;
            }
        }
    }

    void statsSummary();

    config::TerminalProfile const& profile() const { return profile_; }
    config::TerminalProfile& profile() { return profile_; }

    std::chrono::steady_clock::time_point now_;
    config::Config config_;
    std::string profileName_;
    config::TerminalProfile profile_;
    std::string programPath_;
    std::ofstream loggingSink_;
    LoggingSink logger_;
    crispy::text::FontLoader fontLoader_;
    terminal::view::FontConfig fonts_;
    std::unique_ptr<terminal::view::TerminalView> terminalView_;
    FileChangeWatcher configFileChangeWatcher_;
    std::mutex queuedCallsLock_;
    std::deque<std::function<void()>> queuedCalls_;
    QTimer updateTimer_;                            // update() timer used to animate the blinking cursor.
    std::mutex screenUpdateLock_;
    struct Stats {
        std::atomic<uint64_t> updatesSinceRendering = 0;
        std::atomic<uint64_t> consecutiveRenderCount = 0;
    };
    Stats stats_;
#if defined(CONTOUR_VT_METRICS)
    terminal::Metrics terminalMetrics_{};
#endif

    // render state cache
    struct {
        QVector4D backgroundColor{};
        QPoint viewport{};
    } renderStateCache_;
};

} // namespace contour
