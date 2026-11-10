// SPDX-License-Identifier: GPL-3.0-or-later
// The main window: shader errors, per-frame hooks, size, About and show(). Part of run_workspace_session() (app/WorkspaceSession.inc), included in
// order inside its body; it shares that function's locals and is not a standalone file.
    // Capture the application's composited surface on rootless display servers as well.
    window->setAttribute(Qt::WA_TranslucentBackground);
    window->setAutoFillBackground(true);
    viewport->shader_error=[&](const QString &message){if(workspace)workspace->append_console(message);window->statusBar()->showMessage("Shader compile failed");};
    if(numeric_locale_invalid&&workspace)workspace->append_console("Warning: LC_NUMERIC decimal separator is not a dot; GPU shader generation may fail.");
    viewport->frame_done = [&](double ms) {
      branding.ready(window.get());
      if(property_ui_refresh&&!property_ui_scheduled&&!uniform_before&&!stroke_before){
        property_ui_scheduled=true;
        QTimer::singleShot(1,viewport,[&]{
          property_ui_scheduled=false;
          if(!property_ui_refresh||uniform_before||stroke_before)return;
          const auto [id,uid]=*std::exchange(property_ui_refresh,std::nullopt);
          if(document&&document->id()==id&&selected_material>=0&&snapshot.materials[selected_material].session_id==uid)
            refresh_document_ui(false,uid,false);
        });
      }
      if(bts::pending_adjustment_failure){
        if(qEnvironmentVariableIsSet("BTS_BM_ADJUSTMENT_DEMO"))host.diagnostic_adjustment_budget(std::nullopt);auto failure=*std::exchange(bts::pending_adjustment_failure,std::nullopt);const auto error=QString::fromStdString(failure.message);
        std::vector<std::uintptr_t> history;for(const auto &entry:undo_history)history.push_back(reinterpret_cast<std::uintptr_t>(entry.state.get()));
        bool stroke_cancelled=false;const bool unrelated_gesture=bool(stroke_before&&(!stroke_adjustment||stroke_adjustment->layer!=failure.layer));const auto rollback=document&&document->id()==failure.document&&!unrelated_gesture?adjustment_transactions.rollback(failure.snapshot,document->id(),document_revision,history):std::nullopt;
        if(stroke_adjustment&&stroke_before&&document&&document->id()==failure.document&&stroke_adjustment->layer==failure.layer&&document->set(stroke_uid).id==failure.set){if(viewport->painting_)viewport->end_paint(true);else viewport->stroke_end(true);stroke_cancelled=true;}
        if(rollback){
          auto before=std::move(undo_history[*rollback].state);const auto uid=undo_history[*rollback].material_uid;undo_history.erase(undo_history.begin()+*rollback,undo_history.end());redo_history.clear();apply_document_layers(*before,uid,int(before->settings().resolution));document=std::move(before);stroke_cancelled=true;adjustment_transactions.clear();refresh_history_actions();refresh_document_ui(false,uid);viewport->update();
        }
        if(workspace){workspace->append_console(error);workspace->set_error_status(error);if(qEnvironmentVariableIsSet("BTS_BM_ADJUSTMENT_DEMO")){auto refusal=workspace->property("bmRefusal").toJsonObject();refusal["terminal"]=true;refusal["reason"]=error;refusal["needed_bytes"]=qint64(failure.needed);refusal["available_bytes"]=qint64(failure.available);refusal["stroke_cancelled"]=stroke_cancelled;workspace->setProperty("bmRefusal",refusal);}}window->statusBar()->showMessage(error,15000);
      }
      static QElapsedTimer composite_error_timer;
      if(!bts::pending_composite_error.empty()){const auto error=QString::fromStdString(std::exchange(bts::pending_composite_error,{}));if(workspace)workspace->append_console(error);window->statusBar()->showMessage(error,15000);composite_error_timer.start();}
      if(composite_error_timer.isValid()&&composite_error_timer.elapsed()<15000)return;
      window->statusBar()->showMessage(QString("%1  |  %2  |  %3 ms  |  Alt+Left: orbit   Alt+Middle: pan   Alt+Right: dolly   F: frame")
                                        .arg(render_controller&&render_controller->active()?"Cycles":"EEVEE").arg(QString::fromStdString(host.renderer())).arg(ms, 0, 'f', 1)+host.viewport_texture_status());
    };
    window->resize(use_workspace ? QSize(1440, 900) : QSize(1100, 760));
    bts::add_product_about(window.get());
    branding.stage("Initializing viewport…");
    window->show();
