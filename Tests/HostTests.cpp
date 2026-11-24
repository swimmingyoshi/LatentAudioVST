// LatentAudioTests --host <LatentAudioVST.vst3> <modelFolder> [screenshot.png]
//
// Loads the built plugin binary through JUCE's VST3 hosting (like a DAW does),
// loads a model through the plugin state, auditions a position via automation,
// opens the editor in a real window and optionally captures it.
#include <JuceHeader.h>
#include <iostream>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
    void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
    {
        for (auto* param : p.getParameters())
            if (param->getName (100) == name)
                return param;
        return nullptr;
    }

#if JUCE_WINDOWS
    bool captureWindow (juce::Component& window, const juce::File& file)
    {
        auto hwnd = (HWND) window.getWindowHandle();
        RECT rc {};
        GetWindowRect (hwnd, &rc);
        const int w = rc.right - rc.left, h = rc.bottom - rc.top;
        HDC screen = GetDC (nullptr);
        HDC mem = CreateCompatibleDC (screen);
        HBITMAP bmp = CreateCompatibleBitmap (screen, w, h);
        auto old = SelectObject (mem, bmp);
        const bool ok = PrintWindow (hwnd, mem, 2 /* PW_RENDERFULLCONTENT */) != 0;

        juce::Image image (juce::Image::RGB, w, h, true);
        {
            juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
            BITMAPINFO bi {};
            bi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            std::vector<juce::uint8> pixels ((size_t) (w * h * 4));
            GetDIBits (mem, bmp, 0, (UINT) h, pixels.data(), &bi, DIB_RGB_COLORS);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    auto* px = &pixels[(size_t) ((y * w + x) * 4)];
                    data.setPixelColour (x, y, juce::Colour (px[2], px[1], px[0]));
                }
        }
        SelectObject (mem, old);
        DeleteObject (bmp);
        DeleteDC (mem);
        ReleaseDC (nullptr, screen);

        file.deleteFile();
        juce::FileOutputStream out (file);
        return ok && juce::PNGImageFormat().writeImageToStream (image, out);
    }
#endif
}

int hostPlugin (const juce::File& vst3, const juce::File& modelDir, const juce::File& screenshot)
{
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, vst3.getFullPathName());
    if (types.isEmpty())
    {
        std::cout << "FAIL: no plugin found in " << vst3.getFullPathName() << std::endl;
        return 1;
    }
    const auto& desc = *types[0];
    std::cout << "found: " << desc.name << " by " << desc.manufacturerName
              << (desc.isInstrument ? " (instrument)" : " (effect)") << ", " << desc.numOutputChannels << " out" << std::endl;

    juce::String error;
    auto plugin = format.createInstanceFromDescription (desc, 48000.0, 512, error);
    if (plugin == nullptr)
    {
        std::cout << "FAIL: could not instantiate: " << error << std::endl;
        return 1;
    }
    plugin->enableAllBuses();
    plugin->setRateAndBufferSizeDetails (48000.0, 512);
    plugin->prepareToPlay (48000.0, 512);

    // Load the model through the plugin's own state, the way a DAW restores a project.
    // JUCE's VST3 host wraps the plugin's state as base64 in <VST3PluginState><IComponent>.
    // Inside is the plugin's own binary XML, followed by JUCE private data that we keep.
    std::cout << "outputs after enableAllBuses: " << plugin->getTotalNumOutputChannels() << std::endl;
    {
        juce::MemoryBlock hostState;
        plugin->getStateInformation (hostState);
        auto wrapper = juce::AudioProcessor::getXmlFromBinary (hostState.getData(), (int) hostState.getSize());
        auto* component = wrapper != nullptr ? wrapper->getChildByName ("IComponent") : nullptr;
        juce::MemoryBlock inner;
        if (component == nullptr || ! inner.fromBase64Encoding (component->getAllSubText().trim()))
        {
            std::cout << "FAIL: unexpected host state format" << std::endl;
            return 1;
        }
        auto pluginXml = juce::AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize());
        if (pluginXml == nullptr || inner.getSize() < 9)
        {
            std::cout << "FAIL: plugin state is not XML" << std::endl;
            return 1;
        }
        const auto xmlLen = (size_t) juce::ByteOrder::littleEndianInt (static_cast<const char*> (inner.getData()) + 4);
        const size_t tailStart = juce::jmin (inner.getSize(), (size_t) 9 + xmlLen);

        pluginXml->setAttribute ("modelFolder", modelDir.getFullPathName());
        juce::MemoryBlock newInner;
        juce::AudioProcessor::copyXmlToBinary (*pluginXml, newInner);
        newInner.append (static_cast<const char*> (inner.getData()) + tailStart, inner.getSize() - tailStart);

        component->deleteAllTextElements();
        component->addTextElement (newInner.toBase64Encoding());
        juce::MemoryBlock newHostState;
        juce::AudioProcessor::copyXmlToBinary (*wrapper, newHostState);
        plugin->setStateInformation (newHostState.getData(), (int) newHostState.getSize());
        std::cout << "state restored with modelFolder=" << modelDir.getFullPathName() << std::endl;
    }

    // Open the editor in a real window while audio runs.
    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorIfNeeded());
    std::unique_ptr<juce::DocumentWindow> window;
    if (editor != nullptr)
    {
        window = std::make_unique<juce::DocumentWindow> ("LatentAudioVST host test", juce::Colours::black, 0);
        window->setUsingNativeTitleBar (true);
        window->setContentNonOwned (editor.get(), true);
        window->setTopLeftPosition (40, 40);
        window->setVisible (true);
        std::cout << "editor: " << editor->getWidth() << "x" << editor->getHeight() << std::endl;
    }
    else
    {
        std::cout << "FAIL: no editor" << std::endl;
    }

    auto* posX = findParam (*plugin, "Position X");
    auto* posY = findParam (*plugin, "Position Y");
    bool positionOk = true;
    for (auto* p : { posX, posY })
    {
        positionOk = positionOk && p != nullptr && p->isAutomatable();
        if (p != nullptr)
            std::cout << "host parameter list: #" << p->getParameterIndex() << " " << p->getName (64)
                      << (p->isAutomatable() ? " (automatable)" : " (NOT automatable)") << std::endl;
    }
    juce::AudioBuffer<float> buffer (juce::jmax (2, plugin->getTotalNumOutputChannels()), 512);
    juce::MidiBuffer midi;
    double sumSq = 0.0;
    int counted = 0;
    bool finite = true;
    const auto start = juce::Time::getMillisecondCounter();
    bool auditioned = false;
    int rightClicksInPlugin = 0;

    // ~12 s of "real time" audio with the message loop running.
    for (int block = 0; block < 1100; ++block)
    {
        const auto elapsed = juce::Time::getMillisecondCounter() - start;
        if (! auditioned && elapsed > 6000 && posX != nullptr && posY != nullptr)
        {
            posX->setValueNotifyingHost (0.35f);
            posY->setValueNotifyingHost (0.6f);
            auditioned = true;
        }
       #if JUCE_WINDOWS
        // Right-click every part of the knob rows through the real window, like a user.
        if (window != nullptr && block >= 400 && block < 460)
        {
            auto top = static_cast<HWND> (window->getWindowHandle());
            RECT rc;
            GetClientRect (top, &rc);
            POINT pt { (rc.right * (block - 400)) / 60, rc.bottom - 60 };
            HWND target = top;
            for (;;)
            {
                HWND child = ChildWindowFromPointEx (target, pt, CWP_SKIPINVISIBLE);
                if (child == nullptr || child == target) break;
                MapWindowPoints (target, child, &pt, 1);
                target = child;
            }
            rightClicksInPlugin += target != top ? 1 : 0;
            PostMessage (target, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM (pt.x, pt.y));
            PostMessage (target, WM_RBUTTONUP, 0, MAKELPARAM (pt.x, pt.y));
        }
        if (block == 470)
            juce::PopupMenu::dismissAllActiveMenus();
       #endif
        midi.clear();
        plugin->processBlock (buffer, midi);
        if (auditioned)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float s = buffer.getSample (0, i);
                finite = finite && std::isfinite (s);
                sumSq += (double) s * s;
            }
            counted += 512;
        }
        pump (10);
    }

    const double level = counted > 0 ? std::sqrt (sumSq / counted) : 0.0;
    std::cout << "audio after automation: rms=" << level << (finite ? "" : " (NON-FINITE!)") << std::endl;

#if JUCE_WINDOWS
    if (window != nullptr && screenshot != juce::File())
        std::cout << (captureWindow (*window, screenshot) ? "wrote " : "FAIL: capture ") << screenshot.getFullPathName() << std::endl;
#endif

    window.reset();
    if (editor != nullptr)
        plugin->editorBeingDeleted (editor.get());
    editor.reset();
    plugin->releaseResources();
    plugin.reset();

    std::cout << "survived " << rightClicksInPlugin << " right clicks in the editor" << std::endl;
    const bool ok = finite && positionOk && level > 1.0e-4;
    std::cout << (ok ? "HOST TEST PASSED" : "HOST TEST FAILED") << std::endl;
    return ok ? 0 : 1;
}
