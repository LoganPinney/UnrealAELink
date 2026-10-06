// Test-only host harness. Pixel transport and the effect are native C++; this
// script creates a disposable Adobe composition and requests real host renders.
(function () {
    var root = File($.fileName).parent.parent;
    var artifacts = Folder(root.fsName + "/artifacts");
    app.exitAfterLaunchAndEval = true;
    app.exitCode = 1;
    try {
        if (app.project && app.project.numItems > 0) throw new Error("Use a fresh After Effects instance for this test");
        app.newProject();
        app.project.bitsPerChannel = 8;
        var comp = app.project.items.addComp("UnrealAELink host acceptance", 1280, 720, 1, 2, 30);
        var layer = comp.layers.addSolid([0,0,0], "Unreal Beauty", 1280, 720, 1, 2);
        var fx = layer.property("ADBE Effect Parade").addProperty("UnrealAELink.Beauty");
        // Adobe's scripting API omits native button controls from this group.
        if (!fx || !fx.property(1) || fx.property(1).name != "Connect" ||
            !fx.property(2) || fx.property(2).name != "Live" ||
            !fx.property(3) || fx.property(3).name != "Source")
            throw new Error("Native effect or Connect/Live/Source controls did not load");
        function renderFrame(time, target) {
            var item = app.project.renderQueue.items.add(comp);
            item.applyTemplate("Best Settings");
            item.timeSpanStart = time;
            item.timeSpanDuration = comp.frameDuration;
            item.setSettings({"Resolution": comp.resolutionFactor[0] == 2 ? "Half" : "Full"});
            var module = item.outputModule(1);
            module.applyTemplate("TIFF Sequence with Alpha");
            // Adobe owns file creation through its render queue, so no script
            // file/network permission preference needs to be changed.
            module.file = File(target.fsName.replace(/\.tif$/, ".[#####].tif"));
            app.project.renderQueue.render();
            if (item.status != RQItemStatus.DONE) throw new Error("Render failed: " + target.name);
            item.remove();
        }
        renderFrame(0, File(artifacts.fsName + "/ae-disconnected.tif"));
        fx.property(1).setValue(1); // Connect
        $.sleep(1500); // Receiver runs independently on its native worker thread.
        var depths = [8,16,32];
        for (var i=0; i<depths.length; ++i) {
            app.project.bitsPerChannel = depths[i];
            renderFrame((i+1)/30, File(artifacts.fsName + "/ae-beauty-" + depths[i] + ".tif"));
            $.sleep(500);
        }
        app.project.bitsPerChannel = 8;
        fx.property(2).setValue(0); // Freeze latest frame.
        renderFrame(4/30, File(artifacts.fsName + "/ae-frozen-a.tif"));
        $.sleep(700);
        renderFrame(5/30, File(artifacts.fsName + "/ae-frozen-b.tif"));
        fx.property(2).setValue(1); // Resume live subscription.
        $.sleep(1200);
        comp.resolutionFactor = [2,2];
        renderFrame(6/30, File(artifacts.fsName + "/ae-half.tif"));
        comp.resolutionFactor = [1,1];
        fx.property(1).setValue(0); // Disconnect must render opaque black.
        renderFrame(7/30, File(artifacts.fsName + "/ae-disconnected-final.tif"));
        fx.property(1).setValue(1); fx.property(2).setValue(1);
        app.project.save(File(artifacts.fsName + "/UnrealAELinkDemo.aep"));
        app.exitCode = 0;
        app.project.close(CloseOptions.DO_NOT_SAVE_CHANGES);
    } catch (error) {
        // Use Adobe's normal project save API to preserve failure context; no
        // script file/network permission is requested or changed.
        try {
            var info = app.project.items.addComp("FAIL " + String(error).substr(0,150), 1280,720,1,1,30);
            app.project.save(File(artifacts.fsName + "/AfterEffectsHostError.aep"));
            app.project.close(CloseOptions.DO_NOT_SAVE_CHANGES);
        } catch (ignored) {}
        app.exitCode = 1;
    }
})();
