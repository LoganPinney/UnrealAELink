// Actual After Effects Render Queue acceptance, using the native effect.
(function () {
    var root = File($.fileName).parent.parent;
    var artifacts = Folder(root.fsName + "/artifacts/deterministic");
    app.exitAfterLaunchAndEval = true; app.exitCode = 1;
    try {
        if (app.project && app.project.numItems > 0) throw new Error("Use a fresh After Effects instance");
        app.newProject(); app.project.bitsPerChannel = 8;
        app.setMultiFrameRenderingConfig(false, 100); // v0.2 intentionally serial
        var comp = app.project.items.addComp("AE authoritative Sequencer acceptance", 1280,720,1,3,30);
        var layer = comp.layers.addSolid([0,0,0], "Unreal deterministic Beauty",1280,720,1,3);
        layer.startTime = 0; layer.stretch = 100;
        if (layer.canSetTimeRemapEnabled) layer.timeRemapEnabled = false;
        if (layer.timeRemapEnabled) throw new Error("Acceptance layer must have Time Remapping disabled");
        var fx = layer.property("ADBE Effect Parade").addProperty("UnrealAELink.Beauty");
        var mode = fx ? fx.property("Mode") : null;
        if (!mode) {
            var controls = [];
            if (fx) for (var p=1; p<=fx.numProperties; ++p) {
                var control = fx.property(p);
                controls.push(p + ":" + (control ? control.name : "hidden"));
            }
            throw new Error("Deterministic Mode control missing: " + controls.join(","));
        }
        fx.property("Connect").setValue(1); mode.setValue(2);
        function render(start, duration, name) {
            var item = app.project.renderQueue.items.add(comp);
            item.applyTemplate("Best Settings"); item.timeSpanStart = start; item.timeSpanDuration = duration;
            var output = item.outputModule(1); output.applyTemplate("TIFF Sequence with Alpha");
            output.file = File(artifacts.fsName + "/" + name + ".[#####].tif");
            app.project.renderQueue.render();
            if (item.status != RQItemStatus.DONE) throw new Error("Render failed: " + name);
            item.remove();
        }
        var known = [0,30,60];
        for (var i=0; i<known.length; ++i) render(known[i]/30, 1/30, "known-"+known[i]);
        app.purge(PurgeTarget.ALL_CACHES);
        render(0,31/30,"sequential"); // one genuine multi-frame Render Queue job
        var random = [20,3,17,0,29];
        for (var j=0; j<random.length; ++j) {
            app.purge(PurgeTarget.ALL_CACHES); // require real random-access callbacks
            render(random[j]/30,1/30,"random-"+random[j]);
        }
        // Also exercise a different AE rate and a subframe in the 30fps sequence.
        comp.frameRate = 60;
        app.purge(PurgeTarget.ALL_CACHES); render(1/60,1/60,"subframe");
        comp.frameRate = 30000/1001;
        app.purge(PurgeTarget.ALL_CACHES); render(17*comp.frameDuration,comp.frameDuration,"ntsc");
        comp.frameRate = 30;
        app.project.save(File(artifacts.fsName + "/UnrealAELinkDeterministicDemo.aep"));
        app.project.close(CloseOptions.DO_NOT_SAVE_CHANGES); app.exitCode = 0;
    } catch (error) {
        try {
            app.project.items.addComp("FAIL " + String(error).substr(0,150),1280,720,1,1,30);
            app.project.save(File(artifacts.fsName + "/AfterEffectsDeterministicError.aep"));
            app.project.close(CloseOptions.DO_NOT_SAVE_CHANGES);
        } catch (ignored) {}
        app.exitCode = 1;
    }
})();
