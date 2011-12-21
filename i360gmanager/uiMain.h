#ifndef UIMAIN_H
#define UIMAIN_H

#include <QtGui/QMainWindow>
#include <QDir>

#include "ui_uiMain.h"
#include "Iso.h"

using std::vector;

class Main : public QMainWindow
{
	Q_OBJECT

public:
	Main(QWidget *parent = 0, Qt::WFlags flags = 0);
	~Main();


	//
	void refreshDir(QString directory);

private:
	Ui::MainClass ui;
	vector<Iso*> _isos;
};

#endif // UIMAIN_H