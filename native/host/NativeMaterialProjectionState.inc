      // Diagnostic state only; included at the beginning of the GUI state
      // callback, before other explicit actor branches. No interactive work.
      if(qEnvironmentVariableIsSet("BTS_BJ_PROJECTION_UI")){
        state["document_path"]=document_path;
        state["height_pending"]=height_ui.pending();
        state["native_source_pending"]=native_material_loader&&native_material_loader->pending();
        state["redo_count"]=int(redo_history.size());
        state["open_pending"]=opening_project||project_opener.pending();
        state["save_pending"]=bool(active_save)||bool(requested_save)||host.save_project_pending();
        state["open_error"]=workspace->property("projectOpenError").toString();
        state["save_error"]=workspace->property("projectSaveError").toString();
        state["material_error"]=last_material_error;
        if(auto *error=workspace->findChild<QLabel *>("workspaceErrorText");error&&error->isVisible())state["workspace_error"]=error->text();
        state["layer_thumbnails_pending"]=workspace->layer_thumbnails_pending();
        state["cpu_materializations"]=qint64(bts::PaintPixels::recipe_materializations());
        state["cpu_layer_compositions"]=qint64(bts::local_paint_materializations);
        return state;
      }
