#include "WarpEditor.hh"
#include <QApplication>
#include <QMouseEvent>
#include <cassert>
#include <cmath>

int main(int argc,char **argv) {
  QApplication app(argc,argv);
  bts::FillProjection projection;
  projection.mode=5;
  projection.warp_edit=true;
  auto grid=std::make_shared<bts::WarpGrid>(4,3);
  grid->automatic_tangents=false;
  projection.warp=grid;
  const int vertex=5;
  bts::WarpViewContext view;
  view.pane=QRectF(0,0,600,600);
  view.screen=[](const bts::WarpVector &p){return QPointF(300+400*p[0],300+400*p[2]);};
  view.camera_plane=[](QPointF p,const bts::WarpVector &anchor){
    return std::optional<bts::WarpVector>{{(p.x()-300)/400,anchor[1],(p.y()-300)/400}};
  };
  bts::WarpEditor editor;
  editor.synchronize(projection,"test");
  editor.set_selection({vertex});
  std::optional<bts::FillProjection> changed;
  editor.changed=[&](const bts::FillProjection &p){changed=p;};
  editor.error=[](const QString &){assert(false);};
  // A negative U handle has span -1/9. Moving it 0.05 units away from the
  // vertex must lengthen its derivative by 0.45, preserving the signed span.
  const auto from=view.screen(bts::warp_add(grid->nodes[vertex].position,grid->nodes[vertex].du,-1./9));
  const auto to=from+QPointF(-20,0);
  QMouseEvent press(QEvent::MouseButtonPress,from,from,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
  QMouseEvent move(QEvent::MouseMove,to,to,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
  QMouseEvent release(QEvent::MouseButtonRelease,to,to,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
  assert(editor.press(&press,projection,view));
  assert(editor.move(&move,projection,view));
  assert(editor.release(&release,projection,view));
  assert(changed&&changed->warp);
  const auto &node=changed->warp->nodes[vertex];
  assert(std::abs(node.du[0]-1.45)<1e-5);
  assert(node.position==grid->nodes[vertex].position);
  assert(node.dv==grid->nodes[vertex].dv);
  assert(grid->nodes[vertex].du[0]==1); // Preserve the undo snapshot.
}
