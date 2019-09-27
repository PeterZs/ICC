#include <GL/glew.h>
#include "InteractiveWindow.h"
#include "dart/external/lodepng/lodepng.h"
#include "Utils.h"
#include "Configurations.h"
#include "Character.h"
#include <algorithm>
#include <fstream>
#include <boost/filesystem.hpp>
#include <GL/glut.h>

#include <boost/python.hpp>
#include <boost/python/numpy.hpp>

using namespace GUI;
using namespace dart::simulation;
using namespace dart::dynamics;


InteractiveWindow::
InteractiveWindow(std::string network_path, std::string network_type)
	:GLUTWindow(),mTrackCamera(false),mIsAuto(false),mIsCapture(false)
	,mShowPrediction(true),mShowCharacter(true),mSkeletonDrawType(0)
{
	// load configurations
	std::string configuration_filepath = network_path + std::string("/configuration.xml");
	ICC::Configurations::instance().LoadConfigurations(configuration_filepath);
	// set configurations for interactive mode
	ICC::Configurations::instance().setEarlyTermination(false);
	ICC::Configurations::instance().setReferenceType(ICC::ReferenceType::INTERACTIVE);

	this->mEnvironment = new ICC::Environment();
	ICC::Utils::setSkeletonColor(this->mEnvironment->getActor()->getSkeleton(), Eigen::Vector4d(0.73, 0.73, 0.78, 1.0));

	this->mStateSize = this->mEnvironment->getStateSize();
	this->mActionSize = this->mEnvironment->getActionSize();

	this->mDisplayTimeout = 33;
	this->mCurFrame = 0;
	this->mTotalFrame = 0;

	// initial target
	this->mTarget << 0.0, 0.88, 30.0;

	// initialize python objects
	try{
	    Py_Initialize();
		np::initialize();

		this->mTrackingController = p::import("rl.TrackingController").attr("TrackingController")();
		this->mTrackingController.attr("initializeForInteractiveControl")(
			1, // num_slaves
			configuration_filepath, // configurations file path
			this->mStateSize,
			this->mActionSize
		);

		// create motion generator

		this->mMotionGenerator = this->mTrackingController.attr("_motionGenerator");

		// load network
		this->mTrackingController.attr("loadNetworks")(network_path, network_type);
		this->mMotionGenerator.attr("loadNetworks")();
	}
	catch(const  p::error_already_set&)
	{
		PyErr_Print();
	}

	// get initial pose(joint angles and vels)
	this->getPredictions();
	this->mEnvironment->reset();

	this->mRecords.emplace_back(this->mEnvironment->getActor()->getSkeleton()->getPositions());
	this->mTargetRecords.emplace_back(this->mTarget);
}

void 
InteractiveWindow::
getPredictions()
{
	try{
		// clear predictions
		this->mEnvironment->clearReferenceManager();
		// convert target
		p::list target;
		target.append(this->mTarget[2]*100);
		target.append(this->mTarget[0]*100);
		target.append(this->mTarget[1]*100);

		p::list target_wrapper;
		target_wrapper.append(target);

		Eigen::VectorXd pos;
		// get prediction
		pos = ICC::Utils::toEigenVector(np::from_object(this->mMotionGenerator.attr("getReferences")(target_wrapper)), ICC::Configurations::instance().getTCMotionSize());
		this->mEnvironment->addReference(ICC::Utils::convertMGToTC(pos, this->mEnvironment->getActor()->getSkeleton()));
		this->mEnvironment->addReferenceTarget(this->mTarget);


		this->mMotionGenerator.attr("saveState")();


		pos = ICC::Utils::toEigenVector(np::from_object(this->mMotionGenerator.attr("getReferences")(target_wrapper)), ICC::Configurations::instance().getTCMotionSize());
		this->mEnvironment->addReference(ICC::Utils::convertMGToTC(pos, this->mEnvironment->getActor()->getSkeleton()));
		this->mEnvironment->addReferenceTarget(this->mTarget);


		this->mMotionGenerator.attr("loadState")();
	}
	catch(const  p::error_already_set&)
	{
		PyErr_Print();
	}
}

void
InteractiveWindow::
step()
{
	this->getPredictions();
	// Eigen::VectorXd state = this->mEnvironment->getState();
	// np::array converted_state = ICC::Utils::toNumpyArray()
	// Eigen::VectorXd action = ICC::Utils::toEigenVector(np::from_object(this->mTrackingController.attr("_policy").attr("getMeanAction")(converted_state)), this->mActionSize);
	// this->mEnvironment->setAction(action);
}

void
InteractiveWindow::
setFrame(int n)
{
	if( n < 0 || n >= this->mTotalFrame )
	{
		std::cout << "Frame exceeds limits" << std::endl;
		return;
	}


	SkeletonPtr skel = this->mEnvironment->getActor()->getSkeleton();
	Eigen::VectorXd pos = this->mRecords[n];
	skel->setPositions(pos);


	if(this->mTrackCamera){
		Eigen::Vector3d com = this->mEnvironment->getActor()->getSkeleton()->getRootBodyNode()->getCOM();
		Eigen::Isometry3d transform = this->mEnvironment->getActor()->getSkeleton()->getRootBodyNode()->getTransform();
		com[1] = 0.8;

		Eigen::Vector3d camera_pos;
		camera_pos << -3, 1, 1.5;
		camera_pos = camera_pos + com;
		camera_pos[1] = 2;

		mCamera->setCenter(com);
	}


}
void
InteractiveWindow::
nextFrame()
{ 
	this->mCurFrame+=1;
	this->mCurFrame %= this->mTotalFrame;
	this->setFrame(this->mCurFrame);
}

void
InteractiveWindow::
prevFrame()
{
	this->mCurFrame-=1;
	if( this->mCurFrame < 0 ) this->mCurFrame = this->mTotalFrame -1;
	this->setFrame(this->mCurFrame);
}

void
InteractiveWindow::
drawSkeletons()
{
	SkeletonPtr skel = this->mEnvironment->getActor()->getSkeleton();
	if(mShowCharacter){
		GUI::DrawSkeleton(skel, this->mSkeletonDrawType);
	}
}

void
InteractiveWindow::
drawGround()
{
	Eigen::Vector3d com_root = this->mEnvironment->getActor()->getSkeleton()->getRootBodyNode()->getCOM();
	double ground_height = 0.0;
	GUI::DrawGround((int)com_root[0], (int)com_root[2], ground_height);
}

void 
InteractiveWindow::
drawFlag(){
    Eigen::Vector2d goal;
    goal[0] = this->mTargetRecords[mCurFrame][0];
    goal[1] = this->mTargetRecords[mCurFrame][2];

    // glDisable(GL_LIGHTING);
    glLineWidth(10.0);

    Eigen::Vector3d orange= dart::Color::Orange();
    glColor3f(orange[0],orange[1],orange[2]);
    Eigen::Vector3d A, B, C;
    A = Eigen::Vector3d(0, 1.6, 0);
    B = Eigen::Vector3d(0, 1.97, 0);
    C = Eigen::Vector3d(0.3, 1.85, 0.3);

    {
        glPushMatrix();
        glTranslatef(goal[0], 0.05, goal[1]);
        glRotatef(90, 1, 0, 0);
        glColor3f(0.9, 0.53, 0.1);
        GUI::DrawCylinder(0.04, 0.1);
        glPopMatrix();

        glPushMatrix();
        glTranslatef(goal[0], 1.0, goal[1]);
        glRotatef(90, 1, 0, 0);
        glColor3f(0.9, 0.53, 0.1);
        GUI::DrawCylinder(0.02, 2);
        glPopMatrix();

        glPushMatrix();
        glTranslatef(goal[0], 2.039, goal[1]);
        glColor3f(0.9, 0.53, 0.1);
        GUI::DrawSphere(0.04);
        glPopMatrix();
    }
    {
        glPushMatrix();
        glTranslatef(goal[0], 0.0, goal[1]);


        double initTheta = 40.0*std::cos(240/120.0);
        glRotated(initTheta, 0, 1, 0);

        int slice = 100;
        for (int i = 0; i < slice; i++) {
            Eigen::Vector3d p[5];
            p[1] = A + (C - A) * i / slice;
            p[2] = A + (C - A) * (i + 1) / slice;
            p[3] = B + (C - B) * (i + 1) / slice;
            p[4] = B + (C - B) * i / slice;

            for (int j = 4; j >= 1; j--) p[j][0] -= p[1][0], p[j][2] -= p[1][2];

            glPushMatrix();
            glRotated(1.0*std::cos(initTheta + (double)i/slice*2*M_PI) * std::exp(-(double)(slice-i)/slice), 0, 1, 0);

            glBegin(GL_QUADS);
            for (int j = 1; j <= 4; j++) glVertex3d(p[j][0], p[j][1], p[j][2]);
            glEnd();

            glTranslatef(p[2][0], 0, p[2][2]);

        }
        for (int i = 0; i < slice; i++) {
            glPopMatrix();
        }

        glPopMatrix();
    }

}

void
InteractiveWindow::
display() 
{

	glClearColor(0.9, 0.9, 0.9, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	Eigen::Vector3d com_root = this->mEnvironment->getActor()->getSkeleton()->getRootBodyNode()->getCOM();
	Eigen::Vector3d com_front = this->mEnvironment->getActor()->getSkeleton()->getRootBodyNode()->getTransform()*Eigen::Vector3d(0.0, 0.0, 2.0);
	mCamera->apply();
	
	glUseProgram(program);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glPushMatrix();
    glScalef(1.0, -1.0, 1.0);
	initLights(com_root[0], com_root[2], com_front[0], com_front[2]);
	drawSkeletons();
	drawFlag();
	glPopMatrix();
	initLights(com_root[0], com_root[2], com_front[0], com_front[2]);
	drawGround();
	drawSkeletons();
	drawFlag();
	glDisable(GL_BLEND);




	glUseProgram(0);
	glutSwapBuffers();
	if(mIsCapture)
		this->screenshot();
}
void
InteractiveWindow::
keyboard(unsigned char key,int x,int y) 
{
	switch(key)
	{
		case '1' :mShowCharacter= !mShowCharacter;break;
		case '2' :mShowPrediction= !mShowPrediction;break;
		case '[': this->prevFrame();break;
		case ']': this->nextFrame();break;
		case 'o': this->mCurFrame-=99; this->prevFrame();break;
		case 'p': this->mCurFrame+=99; this->nextFrame();break;
		case 's': std::cout << this->mCurFrame << std::endl;break;
		case 'r': this->mCurFrame=0;this->setFrame(this->mCurFrame);break;
		case 'C': mIsCapture = true; break;
		case 't': mTrackCamera = !mTrackCamera; this->setFrame(this->mCurFrame); break;
		case 'T': this->mSkeletonDrawType++; this->mSkeletonDrawType %= 2; break;
		case ' ':
			mIsAuto = !mIsAuto;
			break;
		case 27: exit(0);break;
		default : break;
	}
}
void
InteractiveWindow::
mouse(int button, int state, int x, int y) 
{
	if(button == 3 || button == 4){
		if (button == 3)
		{
			mCamera->pan(0,-5,0,0);
		}
		else
		{
			mCamera->pan(0,5,0,0);
		}
	}
	else{
		if (state == GLUT_DOWN)
		{
			mIsDrag = true;
			mMouseType = button;
			mPrevX = x;
			mPrevY = y;
		}
		else
		{
			mIsDrag = false;
			mMouseType = 0;
		}
	}

	// glutPostRedisplay();
}
void
InteractiveWindow::
motion(int x, int y) 
{
	if (!mIsDrag)
		return;

	int mod = glutGetModifiers();
	if (mMouseType == GLUT_LEFT_BUTTON)
	{
		// if(!mIsRotate)
		mCamera->translate(x,y,mPrevX,mPrevY);
		// else
		// 	mCamera->Rotate(x,y,mPrevX,mPrevY);
	}
	else if (mMouseType == GLUT_RIGHT_BUTTON)
	{
		mCamera->rotate(x,y,mPrevX,mPrevY);
		// switch (mod)
		// {
		// case GLUT_ACTIVE_SHIFT:
		// 	mCamera->Zoom(x,y,mPrevX,mPrevY); break;
		// default:
		// 	mCamera->Pan(x,y,mPrevX,mPrevY); break;		
		// }

	}
	mPrevX = x;
	mPrevY = y;
	// glutPostRedisplay();
}
void
InteractiveWindow::
reshape(int w, int h) 
{
	glViewport(0, 0, w, h);
	mCamera->apply();
}


void
InteractiveWindow::
timer(int value) 
{
	if( mIsAuto )
		this->nextFrame();
	
	glutTimerFunc(mDisplayTimeout, timerEvent,1);
	glutPostRedisplay();
}


void InteractiveWindow::
screenshot() {
  static int count = 0;
  const char directory[8] = "frames";
  const char fileBase[8] = "Capture";
  char fileName[32];

  boost::filesystem::create_directories(directory);
  std::snprintf(fileName, sizeof(fileName), "%s%s%s%.4d.png",
                directory, "/", fileBase, count++);
  int tw = glutGet(GLUT_WINDOW_WIDTH);
  int th = glutGet(GLUT_WINDOW_HEIGHT);

  glReadPixels(0, 0,  tw, th, GL_RGBA, GL_UNSIGNED_BYTE, &mScreenshotTemp[0]);

  // reverse temp2 temp1
  for (int row = 0; row < th; row++) {
    memcpy(&mScreenshotTemp2[row * tw * 4],
           &mScreenshotTemp[(th - row - 1) * tw * 4], tw * 4);
  }

  unsigned result = lodepng::encode(fileName, mScreenshotTemp2, tw, th);

  // if there's an error, display it
  if (result) {
    std::cout << "lodepng error " << result << ": "
              << lodepng_error_text(result) << std::endl;
    return ;
  } else {
    std::cout << "wrote screenshot " << fileName << "\n";
    return ;
  }
}
