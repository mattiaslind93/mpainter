// SPDX-License-Identifier: GPL-3.0-or-later
        const auto parent=[&]{select_content_anchor_demo(anchor_demo_consumer,{},false);};
        const auto resource=[&](int channel,const QString &text){auto *header=workspace->findChild<QToolButton *>(channel?QString("fillChannelHeader%1").arg(channel):QString("fillBaseColorHeader"));auto *menu=header->menu();sets_demo.stop();QTimer::singleShot(0,menu,[menu,text]{for(auto *action:menu->actions())if(action->text()==text){action->trigger();menu->close();return;}qFatal("Fill anchor source menu entry missing: %s",qPrintable(text));});header->showMenu();sets_demo.start(600);};
        const auto source_edit=[&](double value){select_content_anchor_demo(anchor_demo_source,{},false);auto *enabled=workspace->findChild<QCheckBox *>("fillChannelEnabled4");if(enabled->isChecked())qFatal("Source Bump must start disabled");enabled->click();workspace->findChild<QDoubleSpinBox *>("materialBump")->setValue(value);enabled->click();parent();};
        const auto undo_fill=[&]{workspace->findChild<QAction *>("undoDocument")->trigger();parent();};
        switch(sets_stage){
          case 22:parent();edit_material([&](bts::TextureSet &set){auto &source=bts::TextureDocument::layer(set,anchor_demo_source);source.channels.fill(false);source.values.roughness=.7;source.values.bump=-.25;source.displacement.value=-.002;source.users[0].value=2.25;auto &fill=bts::TextureDocument::layer(set,anchor_demo_consumer);fill.mask.reset();fill.fill={};fill.values=bts::new_base_values();fill.channels.fill(true);fill.users[0].enabled=fill.displacement.enabled=true;set.selected_effect=QUuid();set.selected_mask=false;});update_inspection(bts::InspectionMode::Material);break;
          case 23:case 24:case 25:case 26:case 27:resource(sets_stage==23?0:sets_stage==24?2:sets_stage==25?4:sets_stage==26?14:6,"Anchor: Raw content");if(sets_stage==27)update_inspection(static_cast<bts::InspectionMode>(7));break;
          case 28:update_inspection(bts::InspectionMode::Material);workspace->findChild<QComboBox *>("fillAnchorChannel4")->setCurrentIndex(workspace->findChild<QComboBox *>("fillAnchorChannel4")->findData(0));workspace->findChild<QComboBox *>("fillAnchorComponent4")->setCurrentIndex(2);break;
          case 29:workspace->findChild<QComboBox *>("fillAnchorComponent4")->setCurrentIndex(4);break;
          case 30:workspace->findChild<QComboBox *>("fillAnchorChannel4")->setCurrentIndex(workspace->findChild<QComboBox *>("fillAnchorChannel4")->findData(4));workspace->findChild<QComboBox *>("fillAnchorComponent4")->setCurrentIndex(1);break;
          case 31:source_edit(.35);break;
          case 32:case 33:case 34:case 38:case 42:case 43:case 44:case 46:undo_fill();break;
          case 35:workspace->findChild<QPushButton *>("fillGoToAnchor4")->click();break;
          case 36:{auto *list=workspace->findChild<QListWidget *>("anchorReferences");for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole+2).toInt()==4){list->itemActivated(list->item(i));break;}break;}
          case 37:resource(4,"Uniform value");break;
          case 39:save_document(directory+"/fill-anchors.blend");break;
          case 40:open_document(directory+"/fill-anchors.blend");workspace->texture_sets()->setCurrentRow(0);update_inspection(bts::InspectionMode::Material);QTimer::singleShot(200,viewport,[&]{update_inspection(static_cast<bts::InspectionMode>(7));});QTimer::singleShot(350,viewport,[&]{update_inspection(bts::InspectionMode::Material);});break;
          case 41:source_edit(.6);break;
          case 45:bts::select_gui_test_file(workspace,"fillTextureFileDialog",directory+"/anchor-color.png");resource(0,"Load image…");break;
          case 47:workspace->findChild<QAction *>("redoDocument")->trigger();parent();break;
          case 48:save_document(directory+"/fill-image-anchor.blend");break;
          case 49:{const auto path=directory+"/anchor-color.png";if(!QFile::rename(path,path+".unavailable-"+QString::number(QCoreApplication::applicationPid())))qFatal("Cannot retire private anchor image");open_document(directory+"/fill-image-anchor.blend");workspace->texture_sets()->setCurrentRow(0);update_inspection(bts::InspectionMode::Material);QTimer::singleShot(200,viewport,[&]{update_inspection(static_cast<bts::InspectionMode>(7));});QTimer::singleShot(350,viewport,[&]{update_inspection(bts::InspectionMode::Material);});break;}
          case 50:resource(0,"Anchor: Raw content");break;
        }
