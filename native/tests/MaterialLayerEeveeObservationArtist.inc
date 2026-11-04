// External future actor. No viewport update(), renderer setting writes, GPU
// readback or worker polling. The timer observes normal product scheduling.
class EeveeObservationArtist final:public QObject {
 QWidget *window_,*viewport_;QString output_,mesh_;MaterialLayeringArtistHooks hooks_;
 QTimer timer_;QElapsedTimer phase_clock_;QJsonArray observations_,terminals_;
 int stage_=0,phase_=0,history_=0;bool busy_=false,closed_=false,terminal_=false;
 std::uint64_t phase_draw_floor_=0,paint_sequence_=0; qint64 revision_floor_=0;
 std::uint32_t target_uid_=0;QUuid document_id_,set_id_;
 template<class T>T &get(const QString &name){auto *p=window_->findChild<T *>(name);require(p,("EEVEE observation missing actual control: "+name).toUtf8().constData());return *p;}
 const TextureSet &set()const{require(hooks_.document(),"EEVEE observation missing document");for(const auto &s:hooks_.document()->texture_sets())if(s.material_session_uid==hooks_.material())return s;throw std::runtime_error("EEVEE observation selected set disappeared");}
 void click(const char *name){auto &button=get<QAbstractButton>(name);require(button.isVisible()&&button.isEnabled(),"EEVEE observation control unavailable");button.click();}
 void arm(int phase){phase_=phase;terminal_=false;phase_draw_floor_=hooks_.host.eevee_completion_snapshot().host_draw;revision_floor_=hooks_.state()["document_revision"].toVariant().toLongLong();phase_clock_.restart();}
 bool target_ready(const QJsonObject &state)const{
  if(!hooks_.document()||!hooks_.material()||state["open_pending"].toBool()||
     state["composite_pending"].toBool()||state["property_pending"].toBool()||
     state["texture_loading"].toBool())return false;
  if(phase_==1)return !set().material_layering.allocated();
  return phase_==2&&hooks_.material()==target_uid_&&hooks_.document()->id()==document_id_&&set().id==set_id_&&
   set().material_layering.enabled&&set().material_layering.allocated()&&set().material_layering.materials[0].values.roughness==.3125f&&
   state["undo_count"].toInt()==history_+1&&
   state["document_revision"].toVariant().toLongLong()>revision_floor_;
 }
 void write(bool passed,const QString &error={}){
  QSaveFile file(output_+"/pd149-eevee-observation.json");
  require(file.open(QIODevice::WriteOnly),"Cannot open EEVEE observation evidence");
  const QJsonObject proof{{"profile","eevee-completion-observation-only-v1"},
   {"native_pid",qint64(QCoreApplication::applicationPid())},{"passed",passed},{"error",error},
   {"phase",phase_},{"stage",stage_},{"observations",observations_},{"terminal_sampling_observations",terminals_},
   {"scope","Initial New and one real uniform Apply; pointer-free postdraw sampling/queue values plus later Paint; no final-surface, film/GPU fence or graph-epoch proof"},
   {"natural_product_redraw_only",true},{"settings_written",false},{"final_surface_acceptance",false},
   {"shader_math_changed",false},{"snapshot_not_a_completion_epoch",true},
   {"working_space",hooks_.host.color_state()},{"state",hooks_.state()},
   {"last_snapshot",pd149_eevee_observation::snapshot_json(hooks_.host.eevee_completion_snapshot())},{"last_backend_route",hooks_.host.render_state()}};
  require(file.write(QJsonDocument(proof).toJson())>0&&file.commit(),"Cannot retain EEVEE observation evidence");
 }
 void post_paint(std::uint64_t ticket,int phase,int frames_before,qint64 revision_before,std::uint64_t draw_before){
  if(closed_)return;
  const auto state=hooks_.state();const auto s=hooks_.host.eevee_completion_snapshot();const auto route=hooks_.host.render_state();
  const auto revision=state["document_revision"].toVariant().toLongLong();
  const bool same_phase=phase!=0&&phase==phase_;
  const bool new_paint_draw=ticket==paint_sequence_&&state["render_frames"].toInt()>frames_before&&s.host_draw>draw_before;
  const bool fresh_scene=s.status==EeveeCompletionStatus::Captured&&s.host_draw>phase_draw_floor_&&s.target_generation>0;
  // Captured is set only by Host's actual BLENDER_EEVEE Scene draw branch.
  // Device/status are the existing Host route, not a newly inferred RNA engine.
  const bool eevee_route=!route["active"].toBool()&&route["device"].toString()=="EEVEE"&&route["status"].toString()=="EEVEE";
  const bool identity=same_phase&&new_paint_draw&&fresh_scene&&eevee_route&&revision==revision_before;
  const bool observed_terminal=identity&&target_ready(state)&&pd149_eevee_observation::terminal_sampling_state(s);
  auto row=pd149_eevee_observation::snapshot_json(s);row["phase_at_Paint_entry"]=phase;row["current_phase"]=phase_;row["paint_sequence"]=QString::number(ticket);
  row["frames_before_Paint"]=frames_before;row["frames_after_Paint"]=state["render_frames"];row["draw_before_Paint"]=QString::number(draw_before);
  row["phase_draw_floor"]=QString::number(phase_draw_floor_);row["revision_before_Paint"]=revision_before;row["revision_after_Paint"]=revision;
  row["host_backend_route"]=route;row["fresh_current_scene_draw_and_later_Paint"]=identity;row["terminal_sampling_state_observed"]=observed_terminal;
  row["selected_material_uid"]=qint64(hooks_.material());if(hooks_.document()){row["document_id"]=hooks_.document()->id().toString();if(hooks_.material())row["set_id"]=set().id.toString();}
  row["target_ready"]=target_ready(state);row["phase_elapsed_ms"]=phase_clock_.isValid()?phase_clock_.elapsed():0;row["state"]=state;
  observations_.append(row);if(observed_terminal&&!terminal_){terminal_=true;terminals_.append(row);}write(false);
 }
 bool eventFilter(QObject *watched,QEvent *event)override{
  if(!closed_&&watched==viewport_&&event->type()==QEvent::Paint){
   const auto ticket=++paint_sequence_;const auto state=hooks_.state();const int phase=phase_,frames=state["render_frames"].toInt();
   const auto revision=state["document_revision"].toVariant().toLongLong();const auto draw=hooks_.host.eevee_completion_snapshot().host_draw;
   // Runs after the normal Paint handler returns. Never intercept/replace Paint
   // or request another draw; a superseded ticket cannot establish identity.
   QTimer::singleShot(0,this,[this,ticket,phase,frames,revision,draw]{if(closed_)return;try{post_paint(ticket,phase,frames,revision,draw);}catch(const std::exception &e){fail(e.what());}});
  }
  return false;
 }
 void edit_uniform(){
  history_=hooks_.state()["undo_count"].toInt();target_uid_=hooks_.material();document_id_=hooks_.document()->id();set_id_=set().id;click("editUniformMaterialLayering");
  auto &dialog=get<QDialog>("uniformMaterialLayeringDialog");require(dialog.isVisible(),"EEVEE observation real editor unavailable");
  get<QComboBox>("uniformMaterialInput").setCurrentIndex(0);auto &roughness=get<QDoubleSpinBox>("uniformMaterialScalar1");
  require(roughness.isVisible()&&roughness.isEnabled(),"EEVEE observation real Roughness unavailable");roughness.setFocus();
  const auto key=[&](int code,Qt::KeyboardModifiers modifiers,const QString &text){QKeyEvent down(QEvent::KeyPress,code,modifiers,text),up(QEvent::KeyRelease,code,modifiers,text);QApplication::sendEvent(&roughness,&down);QApplication::sendEvent(&roughness,&up);};
  key(Qt::Key_A,Qt::ControlModifier,"a");for(const auto c:QString("0.3125"))key(Qt::Key_unknown,Qt::NoModifier,QString(c));key(Qt::Key_Return,Qt::NoModifier,{});
  require(roughness.value()==.3125,"EEVEE observation real Roughness commit failed");get<QCheckBox>("uniformMaterialLayeringEnabled").setChecked(true);
  arm(2);click("uniformMaterialLayeringApply");
 }
 void advance(){
  if(closed_||busy_)return;const QScopedValueRollback<bool> guard(busy_,true);
  if(stage_==0){stage_=1;get<QAction>("newProject").trigger();return;}
  if(stage_==1){auto &dialog=get<QDialog>("newProjectDialog");require(dialog.isVisible(),"EEVEE observation New dialog missing");get<QComboBox>("projectTemplate").setCurrentIndex(0);get<QLineEdit>("meshPath").setText(mesh_);get<QCheckBox>("preserveUvTiles").setChecked(true);auto &resolution=get<QComboBox>("documentResolution");resolution.setCurrentIndex(resolution.findData(128));arm(1);stage_=2;click("createProject");return;}
  if(phase_clock_.elapsed()>30000){write(false,"No proven terminal sampling state within bounded phase; see unavailable/stale/current observations");retire();QCoreApplication::exit(1);return;}
  if(stage_==2&&terminal_){require(set().tiles==std::set<int>({1001,1002}),"EEVEE observation needs actual two-tile fixture");stage_=3;edit_uniform();return;}
  if(stage_==3&&terminal_){require(terminals_.size()==2,"EEVEE observation missed one phase");write(true);retire();std::puts("PD149_EEVEE_OBSERVATION PASS");QCoreApplication::exit(0);}
 }
 void fail(const char *error)noexcept{std::fprintf(stderr,"PD149_EEVEE_OBSERVATION FAILED phase=%d stage=%d error=%s\n",phase_,stage_,error);try{write(false,QString::fromUtf8(error));}catch(...){std::fputs("PD149_EEVEE_OBSERVATION_EVIDENCE_FAILED\n",stderr);}retire();QCoreApplication::exit(1);}
 public:
 EeveeObservationArtist(QWidget *w,QWidget *v,QString output,QString mesh,MaterialLayeringArtistHooks hooks):QObject(w),window_(w),viewport_(v),output_(std::move(output)),mesh_(std::move(mesh)),hooks_(std::move(hooks)){
  viewport_->installEventFilter(this);connect(&timer_,&QTimer::timeout,this,[this]{try{advance();}catch(const std::exception &e){fail(e.what());}});connect(qApp,&QCoreApplication::aboutToQuit,this,[this]{retire();});timer_.start(33);
 }
 void retire()noexcept{if(closed_)return;closed_=true;timer_.stop();viewport_->removeEventFilter(this);}
};
