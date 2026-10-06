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
        if (!fx || fx.numProperties != 5) throw new Error("Native effect or controls did not load");
        comp.saveFrameToPng(0, File(artifacts.fsName + "/ae-disconnected.png"));
        fx.property(1).setValue(1); // Connect
        $.sleep(1500); // Receiver runs independently on its native worker thread.
        var depths = [8,16,32];
        for (var i=0; i<depths.length; ++i) {
            app.project.bitsPerChannel = depths[i];
            comp.saveFrameToPng((i+1)/30, File(artifacts.fsName + "/ae-beauty-" + depths[i] + ".png"));
            $.sleep(500);
        }
        app.project.bitsPerChannel = 8;
        fx.property(2).setValue(0); // Freeze latest frame.
        comp.saveFrameToPng(4/30, File(artifacts.fsName + "/ae-frozen-a.png"));
        $.sleep(700);
        comp.saveFrameToPng(5/30, File(artifacts.fsName + "/ae-frozen-b.png"));
        fx.property(2).setValue(1); // Resume live subscription.
        $.sleep(1200);
        comp.resolutionFactor = [2,2];
        comp.saveFrameToPng(6/30, File(artifacts.fsName + "/ae-half.png"));
        comp.resolutionFactor = [1,1];
        fx.property(1).setValue(0); // Disconnect must render opaque black.
        comp.saveFrameToPng(7/30, File(artifacts.fsName + "/ae-disconnected-final.png"));
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
