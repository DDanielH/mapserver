#include "mapserv.h"

#ifdef USE_EGIS 
#include <errLog.h>  	// egis - includes
#include "egis.h"
#include "globalStruct.h"
#include "egisHTML.h"
 
//OV -egis- for local debugging, if set various message will be 
// printed to /tmp/egisError.log

#define myDebug 0 
#define myDebugHtml 0

char errLogMsg[80];	// egis - for storing and printing error messages
#endif

static double inchesPerUnit[6]={1, 12, 63360.0, 39.3701, 39370.1, 4374754};

/*
** Various function prototypes
*/
void returnPage(char *, int);
void returnURL(char *, int);

static void redirect(char *url)
{
  printf("Status: 302 Found\n");
  printf("Uri: %s\n", url);
  printf("Location: %s\n", url);
  printf("Content-type: text/html%c%c",10,10);
  fflush(stdout);
  return;
}

int writeLog(int show_error)
{
  FILE *stream;
  int i;
  time_t t;

  if(!Map) return(0);
  if(!Map->web.log) return(0);
  
  if((stream = fopen(Map->web.log,"a")) == NULL) {
    msSetError(MS_IOERR, Map->web.log, "writeLog()");
    return(-1);
  }

  t = time(NULL);
  fprintf(stream,"%s,",chop(ctime(&t)));
  fprintf(stream,"%d,",(int)getpid());
  
  if(getenv("REMOTE_ADDR") != NULL)
    fprintf(stream,"%s,",getenv("REMOTE_ADDR"));
  else
    fprintf(stream,"NULL,");
 
  fprintf(stream,"%s,",Map->name);
  fprintf(stream,"%d,",Mode);

  fprintf(stream,"%f %f %f %f,", Map->extent.minx, Map->extent.miny, Map->extent.maxx, Map->extent.maxy);

  fprintf(stream,"%f %f,", MapPnt.x, MapPnt.y);

  for(i=0;i<NumLayers;i++)
    fprintf(stream, "%s ", Layers[i]);
  fprintf(stream,",");

  if(show_error == MS_TRUE)
    msWriteError(stream);
  else
    fprintf(stream, "normal execution");

  fprintf(stream,"\n");

  fclose(stream);
  return(0);
}

void writeError()
{
  int i;

  writeLog(MS_TRUE);

  if(!Map) {
    printf("Content-type: text/html%c%c",10,10);
    printf("<HTML>\n");
    printf("<HEAD><TITLE>MapServer Message</TITLE></HEAD>\n");
    printf("<!-- %s -->\n", msGetVersion());
    printf("<BODY BGCOLOR=\"#FFFFFF\">\n");
    msWriteError(stdout);
    printf("</BODY></HTML>");
    exit(0);
  }

  if((ms_error.code == MS_NOTFOUND) && (Map->web.empty)) {
    redirect(Map->web.empty);
  } else {
    if(Map->web.error) {      
      redirect(Map->web.error);
    } else {
      printf("Content-type: text/html%c%c",10,10);
      printf("<HTML>\n");
      printf("<HEAD><TITLE>MapServer Message</TITLE></HEAD>\n");
      printf("<!-- %s -->\n", msGetVersion());
      printf("<BODY BGCOLOR=\"#FFFFFF\">\n");
      msWriteError(stdout);
      printf("</BODY></HTML>");
    }
  }

  msFreeMap(Map);
  for(i=0;i<NumEntries;i++) {
    free(Entries[i].val);
    free(Entries[i].name);
  }
    
  free(Item);
  free(Value);      
  free(QueryFile);
  free(QueryLayer);      
  free(SelectLayer);

  for(i=0;i<NumLayers;i++)
    free(Layers[i]);

  exit(0); // bail
}

/*
** Converts a string (e.g. form parameter) to a double, first checking the format against
** a regular expression. Dumps an error immediately if the format test fails.
*/
static double getNumeric(regex_t re, char *s)
{
  regmatch_t *match;
  
  if((match = (regmatch_t *)malloc(sizeof(regmatch_t))) == NULL) {
    msSetError(MS_MEMERR, NULL, "getNumeric()");
    writeError();
  }
  
  if(regexec(&re, s, (size_t)1, match, 0) == REG_NOMATCH) {
    free(match);
    msSetError(MS_TYPEERR, NULL, "getNumeric()"); 
    writeError();
  }
  
  if(strlen(s) != (match->rm_eo - match->rm_so)) { /* whole string must match */    
    free(match);
    msSetError(MS_TYPEERR, NULL, "getNumeric()"); 
    writeError();
  }
  
  free(match);
  return(atof(s));
}

/*
** Process CGI parameters.
*/
void loadForm()
{
  int i, n;
  char **tokens, *tmpstr;
  regex_t re;

  for(i=0;i<NumEntries;i++) // find the mapfile parameter first
    if(strcasecmp(Entries[i].name, "map") == 0) break;
  
  if(i == NumEntries) {
    if(getenv("MS_MAPFILE")) // has a default mapfile has not been set
      Map = msLoadMap(Entries[i].val);      
    else {
      msSetError(MS_WEBERR, "CGI variable \"map\" is not set.", "loadForm()"); // no default, outta here
      writeError();
    }
  } else
    Map = msLoadMap(Entries[i].val);

  if(!Map) writeError();

  if(regcomp(&re, NUMEXP, REG_EXTENDED) != 0) { // what is a number
    msSetError(MS_REGEXERR, NULL, "loadForm()"); 
    writeError();
  }

  for(i=0;i<NumEntries;i++) { // now process the rest of the form variables

    if(strlen(Entries[i].val) == 0)
      continue;
    
    if(strcasecmp(Entries[i].name,"queryfile") == 0) {      
      QueryFile = strdup(Entries[i].val);
      continue;
    }
    
    if(strcasecmp(Entries[i].name,"savequery") == 0) {
      SaveQuery = MS_TRUE;
      continue;
    }
    
    if(strcasecmp(Entries[i].name,"savemap") == 0) {      
      SaveMap = MS_TRUE;
      continue;
    }

    if(strcasecmp(Entries[i].name,"zoom") == 0) {
      Zoom = getNumeric(re, Entries[i].val);      
      if((Zoom > MAXZOOM) || (Zoom < MINZOOM)) {
	msSetError(MS_WEBERR, "Zoom value out of range.", "loadForm()");
	writeError();
      }
      continue;
    }

    if(strcasecmp(Entries[i].name,"zoomdir") == 0) {
      ZoomDirection = getNumeric(re, Entries[i].val);
      if((ZoomDirection != -1) && (ZoomDirection != 1) && (ZoomDirection != 0)) {
	msSetError(MS_WEBERR, "Zoom direction must be 1, 0 or -1.", "loadForm()");
	writeError();
      }
      continue;
    }

    if(strcasecmp(Entries[i].name,"zoomsize") == 0) { // absolute zoom magnitude
      ZoomSize = getNumeric(re, Entries[i].val);      
      if((ZoomSize > MAXZOOM) || (ZoomSize < 1)) {
	msSetError(MS_WEBERR, "Invalid zoom size.", "loadForm()");
	writeError();
      }	
      continue;
    }
    
    if(strcasecmp(Entries[i].name,"imgext") == 0) { // extent of an existing image in a web application
      tokens = split(Entries[i].val, ' ', &n);

      if(!tokens) {
	msSetError(MS_MEMERR, NULL, "loadForm()");
	writeError();
      }

      if(n != 4) {
	msSetError(MS_WEBERR, "Not enough arguments for imgext.", "loadForm()");
	writeError();
      }

      ImgExt.minx = getNumeric(re, tokens[0]);
      ImgExt.miny = getNumeric(re, tokens[1]);
      ImgExt.maxx = getNumeric(re, tokens[2]);
      ImgExt.maxy = getNumeric(re, tokens[3]);

      msFreeCharArray(tokens, 4);
      continue;
    }

    if(strcasecmp(Entries[i].name,"searchmap") == 0) {      
      SearchMap = MS_TRUE;
      continue;
    }

    if(strcasecmp(Entries[i].name,"id") == 0) {      
      strncpy(Id, Entries[i].val, IDSIZE);
      continue;
    }

    if(strcasecmp(Entries[i].name,"mapext") == 0) { // extent of the new map or query

      if(strncasecmp(Entries[i].val,"shape",5) == 0)
        UseShapes = MS_TRUE;
      else {
	tokens = split(Entries[i].val, ' ', &n);
	
	if(!tokens) {
	  msSetError(MS_MEMERR, NULL, "loadForm()");
	  writeError();
	}
	
	if(n != 4) {
	  msSetError(MS_WEBERR, "Not enough arguments for mapext.", "loadForm()");
	  writeError();
	}
	
	Map->extent.minx = getNumeric(re, tokens[0]);
	Map->extent.miny = getNumeric(re, tokens[1]);
	Map->extent.maxx = getNumeric(re, tokens[2]);
	Map->extent.maxy = getNumeric(re, tokens[3]);	
	
	msFreeCharArray(tokens, 4);
	
#ifdef USE_PROJ
	if(Map->projection.proj && (Map->extent.minx >= -180.0 && Map->extent.minx <= 180.0) && (Map->extent.miny >= -90.0 && Map->extent.miny <= 90.0))
	  msProjectRect(NULL, Map->projection.proj, &(Map->extent)); // extent is a in lat/lon
#endif

	if((Map->extent.minx != Map->extent.maxx) && (Map->extent.miny != Map->extent.maxy)) { // extent seems ok
	  CoordSource = FROMUSERBOX;
	  QueryCoordSource = FROMUSERBOX;
	}
      }

      continue;
    }

    if(strcasecmp(Entries[i].name,"minx") == 0) { // extent of the new map, in pieces
      Map->extent.minx = getNumeric(re, Entries[i].val);      
      continue;
    }
    if(strcasecmp(Entries[i].name,"maxx") == 0) {      
      Map->extent.maxx = getNumeric(re, Entries[i].val);
      continue;
    }
    if(strcasecmp(Entries[i].name,"miny") == 0) {
      Map->extent.miny = getNumeric(re, Entries[i].val);
      continue;
    }
    if(strcasecmp(Entries[i].name,"maxy") == 0) {
      Map->extent.maxy = getNumeric(re, Entries[i].val);
      CoordSource = FROMUSERBOX;
      QueryCoordSource = FROMUSERBOX;
      continue;
    } 

    if(strcasecmp(Entries[i].name,"mapxy") == 0) { // user map coordinate
      
      if(strncasecmp(Entries[i].val,"shape",5) == 0) {
        UseShapes = MS_TRUE;	
      } else {
	tokens = split(Entries[i].val, ' ', &n);

	if(!tokens) {
	  msSetError(MS_MEMERR, NULL, "loadForm()");
	  writeError();
	}
	
	if(n != 2) {
	  msSetError(MS_WEBERR, "Not enough arguments for mapxy.", "loadForm()");
	  writeError();
	}
	
	MapPnt.x = getNumeric(re, tokens[0]);
	MapPnt.y = getNumeric(re, tokens[1]);
	
	msFreeCharArray(tokens, 2);

#ifdef USE_PROJ
	if(Map->projection.proj && (MapPnt.x >= -180.0 && MapPnt.x <= 180.0) && (MapPnt.y >= -90.0 && MapPnt.y <= 90.0))
	  msProjectPoint(NULL, Map->projection.proj, &MapPnt); // point is a in lat/lon
#endif

	if(CoordSource == NONE) { // don't override previous settings (i.e. buffer or scale )
	  CoordSource = FROMUSERPNT;
	  QueryCoordSource = FROMUSERPNT;
	}
      }
      continue;
    }

    if(strcasecmp(Entries[i].name,"mapshape") == 0) { // query shape
      lineObj line={0,NULL};
      char **tmp=NULL;
      int n, j;
      
      tmp = split(Entries[i].val, ' ', &n);

      if((line.point = (pointObj *)malloc(sizeof(pointObj)*(n/2))) == NULL) {
	msSetError(MS_MEMERR, NULL, "loadForm()");
	writeError();
      }
      line.numpoints = n/2;

      for(j=0; j<n/2; j++) {
	line.point[j].x = atof(tmp[2*j]);
	line.point[j].y = atof(tmp[2*j+1]);

#ifdef USE_PROJ
	if(Map->projection.proj && (line.point[j].x >= -180.0 && line.point[j].x <= 180.0) && (line.point[j].y >= -90.0 && line.point[j].y <= 90.0))
	  msProjectPoint(NULL, Map->projection.proj, &line.point[j]); // point is a in lat/lon
#endif
      }

      if(msAddLine(&SelectShape, &line) == -1) writeError();
      msFreeCharArray(tmp, n);

      QueryCoordSource = FROMUSERSHAPE;
      continue;
    }

    if(strcasecmp(Entries[i].name,"img.x") == 0) { // mouse click, in pieces
      ImgPnt.x = getNumeric(re, Entries[i].val);
      if((ImgPnt.x > MAXCLICK) || (ImgPnt.x < MINCLICK)) {
	msSetError(MS_WEBERR, "Coordinate out of range.", "loadForm()");
	writeError();
      }
      CoordSource = FROMIMGPNT;
      QueryCoordSource = FROMIMGPNT;
      continue;
    }
    if(strcasecmp(Entries[i].name,"img.y") == 0) {
      ImgPnt.y = getNumeric(re, Entries[i].val);      
      if((ImgPnt.y > MAXCLICK) || (ImgPnt.y < MINCLICK)) {
	msSetError(MS_WEBERR, "Coordinate out of range.", "loadForm()");
	writeError();
      }
      CoordSource = FROMIMGPNT;
      QueryCoordSource = FROMIMGPNT;
      continue;
    }

    if(strcasecmp(Entries[i].name,"imgxy") == 0) { // mouse click, single variable
      if(CoordSource == FROMIMGPNT)
	continue;

      tokens = split(Entries[i].val, ' ', &n);

      if(!tokens) {
	msSetError(MS_MEMERR, NULL, "loadForm()");
	writeError();
      }

      if(n != 2) {
	msSetError(MS_WEBERR, "Not enough arguments for imgxy.", "loadForm()");
	writeError();
      }

      ImgPnt.x = getNumeric(re, tokens[0]);
      ImgPnt.y = getNumeric(re, tokens[1]);

      msFreeCharArray(tokens, 2);

      if((ImgPnt.x > MAXCLICK) || (ImgPnt.x < MINCLICK) || (ImgPnt.y > MAXCLICK) || (ImgPnt.y < MINCLICK)) {
	msSetError(MS_WEBERR, "Reference map coordinate out of range.", "loadForm()");
	writeError();
      }

      if(CoordSource == NONE) { // override nothing since this parameter is usually used to hold a default value
	CoordSource = FROMIMGPNT;
	QueryCoordSource = FROMIMGPNT;
      }
      continue;
    }

    if(strcasecmp(Entries[i].name,"imgbox") == 0) { // selection box (eg. mouse drag)
      tokens = split(Entries[i].val, ' ', &n);
      
      if(!tokens) {
	msSetError(MS_MEMERR, NULL, "loadForm()");
	writeError();
      }
      
      if(n != 4) {
	msSetError(MS_WEBERR, "Not enough arguments for imgbox.", "loadForm()");
	writeError();
      }
      
      ImgBox.minx = getNumeric(re, tokens[0]);
      ImgBox.miny = getNumeric(re, tokens[1]);
      ImgBox.maxx = getNumeric(re, tokens[2]);
      ImgBox.maxy = getNumeric(re, tokens[3]);
      
      msFreeCharArray(tokens, 4);

      if((ImgBox.minx != ImgBox.maxx) && (ImgBox.miny != ImgBox.maxy)) { // must not degenerate into a point
	CoordSource = FROMIMGBOX;
	QueryCoordSource = FROMIMGBOX;
      }
      continue;
    }

    if(strcasecmp(Entries[i].name,"imgshape") == 0) { // shape given in image coordinates
      lineObj line={0,NULL};
      char **tmp=NULL;
      int n, j;
      
      tmp = split(Entries[i].val, ' ', &n);

      if((line.point = (pointObj *)malloc(sizeof(pointObj)*(n/2))) == NULL) {
	msSetError(MS_MEMERR, NULL, "loadForm()");
	writeError();
      }
      line.numpoints = n/2;

      for(j=0; j<n/2; j++) {
	line.point[j].x = atof(tmp[2*j]);
	line.point[j].y = atof(tmp[2*j+1]);
      }

      if(msAddLine(&SelectShape, &line) == -1) writeError();
      msFreeCharArray(tmp, n);

      QueryCoordSource = FROMIMGSHAPE;
      continue;
    }

    if(strcasecmp(Entries[i].name,"ref.x") == 0) { // mouse click in reference image, in pieces
      RefPnt.x = getNumeric(re, Entries[i].val);      
      if((RefPnt.x > MAXCLICK) || (RefPnt.x < MINCLICK)) {
	msSetError(MS_WEBERR, "Coordinate out of range.", "loadForm()");
	writeError();
      }
      CoordSource = FROMREFPNT;
      continue;
    }
    if(strcasecmp(Entries[i].name,"ref.y") == 0) {
      RefPnt.y = getNumeric(re, Entries[i].val); 
      if((RefPnt.y > MAXCLICK) || (RefPnt.y < MINCLICK)) {
	msSetError(MS_WEBERR, "Coordinate out of range.", "loadForm()");
	writeError();
      }
      CoordSource = FROMREFPNT;
      continue;
    }

    if(strcasecmp(Entries[i].name,"refxy") == 0) { /* mouse click in reference image, single variable */
      tokens = split(Entries[i].val, ' ', &n);

      if(!tokens) {
	msSetError(MS_MEMERR, NULL, "loadForm()");
	writeError();
      }

      if(n != 2) {
	msSetError(MS_WEBERR, "Not enough arguments for imgxy.", "loadForm()");
	writeError();
      }

      RefPnt.x = getNumeric(re, tokens[0]);
      RefPnt.y = getNumeric(re, tokens[1]);

      msFreeCharArray(tokens, 2);
      
      if((RefPnt.x > MAXCLICK) || (RefPnt.x < MINCLICK) || (RefPnt.y > MAXCLICK) || (RefPnt.y < MINCLICK)) {
	msSetError(MS_WEBERR, "Reference map coordinate out of range.", "loadForm()");
	writeError();
      }
      
      CoordSource = FROMREFPNT;
      continue;
    }

    if(strcasecmp(Entries[i].name,"buffer") == 0) { // radius (map units), actually 1/2 square side
      Buffer = getNumeric(re, Entries[i].val);      
      CoordSource = FROMBUF;
      QueryCoordSource = FROMUSERPNT;
      continue;
    }

    if(strcasecmp(Entries[i].name,"scale") == 0) { // scale for new map
      Map->scale = getNumeric(re, Entries[i].val);      
      if(Map->scale <= 0) {
	msSetError(MS_WEBERR, "Scale out of range.", "loadForm()");
	writeError();
      }
      CoordSource = FROMSCALE;
      QueryCoordSource = FROMUSERPNT;
      continue;
    }
    
    if(strcasecmp(Entries[i].name,"imgsize") == 0) { // size of existing image (pixels)
      tokens = split(Entries[i].val, ' ', &n);

      if(!tokens) {
	msSetError(MS_MEMERR, NULL, "loadForm()");
	writeError();
      }

      if(n != 2) {
	msSetError(MS_WEBERR, "Not enough arguments for imgsize.", "loadForm()");
	writeError();
      }

      ImgCols = getNumeric(re, tokens[0]);
      ImgRows = getNumeric(re, tokens[1]);

      msFreeCharArray(tokens, 2);
      
      if(ImgCols > MAXIMGSIZE || ImgRows > MAXIMGSIZE || ImgCols < 0 || ImgRows < 0) {
	msSetError(MS_WEBERR, "Image size out of range.", "loadForm()");
	writeError();
      }
 
      continue;
    }

    if(strcasecmp(Entries[i].name,"mapsize") == 0) { // size of new map (pixels)
      tokens = split(Entries[i].val, ' ', &n);

      if(!tokens) {
	msSetError(MS_MEMERR, NULL, "loadForm()");
	writeError();
      }

      if(n != 2) {
	msSetError(MS_WEBERR, "Not enough arguments for mapsize.", "loadForm()");
	writeError();
      }

      Map->width = getNumeric(re, tokens[0]);
      Map->height = getNumeric(re, tokens[1]);

      msFreeCharArray(tokens, 2);
      
      if(Map->width > MAXIMGSIZE || Map->height > MAXIMGSIZE || Map->width < 0 || Map->height < 0) {
	msSetError(MS_WEBERR, "Image size out of range.", "loadForm()");
	writeError();
      }
      continue;
    }

    if(strncasecmp(Entries[i].name,"layers", 6) == 0) { // turn a set of layers, delimited by spaces, on
      int num_layers=0, l;
      char **layers=NULL;

      layers = split(Entries[i].val, ' ', &(num_layers));
      for(l=0; l<num_layers; l++)
	Layers[NumLayers+l] = strdup(layers[l]);
      NumLayers += l;

      msFreeCharArray(layers, num_layers);
      num_layers = 0;
      continue;
    }

    if(strncasecmp(Entries[i].name,"layer", 5) == 0) { // turn a single layer/group on
      Layers[NumLayers] = strdup(Entries[i].val);
      NumLayers++;
      continue;
    }

    if(strcasecmp(Entries[i].name,"qlayer") == 0) { // layer to query (i.e search)
      QueryLayer = strdup(Entries[i].val);
      continue;
    }

    if(strcasecmp(Entries[i].name,"slayer") == 0) { // layer to select (for feature based search)
      SelectLayer = strdup(Entries[i].val);
      continue;
    }

    if(strcasecmp(Entries[i].name,"item") == 0) { // search item
      Item = strdup(Entries[i].val);
      continue;
    }

    if(strcasecmp(Entries[i].name,"value") == 0) { // search expression
      if(!Value)
	Value = strdup(Entries[i].val);
      else { /* need to append */
	tmpstr = strdup(Value);
	free(Value);
	Value = (char *)malloc(strlen(tmpstr)+strlen(Entries[i].val)+2);
	sprintf(Value, "%s|%s", tmpstr, Entries[i].val);
	free(tmpstr);
      }
      continue;
    }

    if(strcasecmp(Entries[i].name,"template") == 0) { // template file, common change hence the simple parameter
      free(Map->web.template);
      Map->web.template = strdup(Entries[i].val);      
      continue;
    }

#ifdef USE_EGIS

    // OV - egis - additional token "none" is defined to create somewhat
    // mutual exculsiveness between mapserver and egis
 
    if(strcasecmp(Entries[i].name,"egis") == 0)
    {
        if(strcasecmp(Entries[i].val,"none") != 0)
        {
                Mode = PROCESSING;
        }
        continue;
    }
#endif

    if(strcasecmp(Entries[i].name,"mode") == 0) { // set operation mode
      if(strcasecmp(Entries[i].val,"browse") == 0) {
        Mode = BROWSE;
        continue;
      }
      if(strcasecmp(Entries[i].val,"zoomin") == 0) {
	ZoomDirection = 1;
        Mode = BROWSE;
        continue;
      }
      if(strcasecmp(Entries[i].val,"zoomout") == 0) {
	ZoomDirection = -1;
        Mode = BROWSE;
        continue;
      }
      if(strcasecmp(Entries[i].val,"featurequery") == 0) {
        Mode = FEATUREQUERY;
        continue;
      }
      if(strcasecmp(Entries[i].val,"featurenquery") == 0) {
        Mode = FEATURENQUERY;
        continue;
      }
      if(strcasecmp(Entries[i].val,"itemquery") == 0) {
        Mode = ITEMQUERY;
        continue;
      }
      if(strcasecmp(Entries[i].val,"query") == 0) {
        Mode = QUERY;
        continue;
      }
      if(strcasecmp(Entries[i].val,"nquery") == 0) {
        Mode = NQUERY;
        continue;
      }
      if(strcasecmp(Entries[i].val,"featurequerymap") == 0) {
        Mode = FEATUREQUERYMAP;
        continue;
      }
      if(strcasecmp(Entries[i].val,"featurenquerymap") == 0) {
        Mode = FEATURENQUERYMAP;
        continue;
      }
      if(strcasecmp(Entries[i].val,"itemquerymap") == 0) {
        Mode = ITEMQUERYMAP;
        continue;
      }
      if(strcasecmp(Entries[i].val,"querymap") == 0) {
        Mode = QUERYMAP;
        continue;
      }
      if(strcasecmp(Entries[i].val,"nquerymap") == 0) {
        Mode = NQUERYMAP;
        continue;
      }
      if(strcasecmp(Entries[i].val,"map") == 0) {
        Mode = MAP;
        continue;
      }
      if(strcasecmp(Entries[i].val,"legend") == 0) {
        Mode = LEGEND;
        continue;
      }
      if(strcasecmp(Entries[i].val,"scalebar") == 0) {
        Mode = SCALEBAR;
        continue;
      }
      if(strcasecmp(Entries[i].val,"reference") == 0) {
        Mode = REFERENCE;
        continue;
      }
      if(strcasecmp(Entries[i].val,"coordinate") == 0) {
        Mode = COORDINATE;
        continue;
      }

#ifdef USE_EGIS
      //OV -egis- may be unsafe - test it properly for side effects- raju

      if(strcasecmp(Entries[i].val,"none") == 0) {
        continue;
      }

      // OV - Defined any new modes?
      errLogMsg[0] = '\0';
      sprintf(errLogMsg, "Invalid mode :: %s", Entries[i].val);

#endif

      msSetError(MS_WEBERR, "Invalid mode.", "loadForm()");
      writeError();
    }

    if(strncasecmp(Entries[i].name,"map_",4) == 0) { // check to see if there are any additions to the mapfile
      if(msLoadMapString(Map, Entries[i].name, Entries[i].val) == -1)
	writeError();
      continue;
    }
  }

  regfree(&re);

  if(ZoomSize != 0) { // use direction and magnitude to calculate zoom
    if(ZoomDirection == 0) {
      fZoom = 1;
    } else {
      fZoom = ZoomSize*ZoomDirection;
      if(fZoom < 0)
	fZoom = 1.0/MS_ABS(fZoom);
    }
  } else { // use single value for zoom
    if((Zoom >= -1) && (Zoom <= 1)) {
      fZoom = 1; // pan
    } else {
      if(Zoom < 0)
	fZoom = 1.0/MS_ABS(Zoom);
      else
	fZoom = Zoom;
    }
  }

  if(ImgRows == -1) ImgRows = Map->height;
  if(ImgCols == -1) ImgCols = Map->width;  
  if(Map->height == -1) Map->height = ImgRows;
  if(Map->width == -1) Map->width = ImgCols;
}

/*
** Is a particular layer or group on, that is was it requested explicitly by the user.
*/
int isOn(char *name, char *group)
{
  int i;

  for(i=0;i<NumLayers;i++) {
    if(name && strcmp(Layers[i], name) == 0)  return(MS_TRUE);
    if(group && strcmp(Layers[i], group) == 0) return(MS_TRUE);
  }

  return(MS_FALSE);
}

/*
** Sets the map extent under a variety of scenarios.
*/
void setExtent() 
{
  double cellx,celly,cellsize;

  switch(CoordSource) {
  case FROMUSERBOX: /* user passed in a map extent */
    break;
  case FROMIMGBOX: /* fully interactive web, most likely with java front end */
    cellx = (ImgExt.maxx-ImgExt.minx)/(ImgCols-1);
    celly = (ImgExt.maxy-ImgExt.miny)/(ImgRows-1);
    Map->extent.minx = ImgExt.minx + cellx*ImgBox.minx;
    Map->extent.maxx = ImgExt.minx + cellx*ImgBox.maxx;
    Map->extent.miny = ImgExt.maxy - celly*ImgBox.maxy;
    Map->extent.maxy = ImgExt.maxy - celly*ImgBox.miny;
    break;
  case FROMIMGPNT:
    cellx = (ImgExt.maxx-ImgExt.minx)/(ImgCols-1);
    celly = (ImgExt.maxy-ImgExt.miny)/(ImgRows-1);
    MapPnt.x = ImgExt.minx + cellx*ImgPnt.x;
    MapPnt.y = ImgExt.maxy - celly*ImgPnt.y;
    Map->extent.minx = MapPnt.x - .5*((ImgExt.maxx - ImgExt.minx)/fZoom);
    Map->extent.miny = MapPnt.y - .5*((ImgExt.maxy - ImgExt.miny)/fZoom);
    Map->extent.maxx = MapPnt.x + .5*((ImgExt.maxx - ImgExt.minx)/fZoom);
    Map->extent.maxy = MapPnt.y + .5*((ImgExt.maxy - ImgExt.miny)/fZoom);
    break;
  case FROMREFPNT:
    cellx = (Map->reference.extent.maxx -  Map->reference.extent.minx)/(Map->reference.width-1);
    celly = (Map->reference.extent.maxy -  Map->reference.extent.miny)/(Map->reference.height-1);
    MapPnt.x = Map->reference.extent.minx + cellx*RefPnt.x;
    MapPnt.y = Map->reference.extent.maxy - celly*RefPnt.y;    
    Map->extent.minx = MapPnt.x - .5*(ImgExt.maxx - ImgExt.minx);
    Map->extent.miny = MapPnt.y - .5*(ImgExt.maxy - ImgExt.miny);
    Map->extent.maxx = MapPnt.x + .5*(ImgExt.maxx - ImgExt.minx);
    Map->extent.maxy = MapPnt.y + .5*(ImgExt.maxy - ImgExt.miny);
    break;
  case FROMBUF: /* user supplied a point and a buffer */
    Map->extent.minx = MapPnt.x - Buffer;
    Map->extent.miny = MapPnt.y - Buffer;
    Map->extent.maxx = MapPnt.x + Buffer;
    Map->extent.maxy = MapPnt.y + Buffer;
    break;
  case FROMSCALE: /* user supplied a point and a scale */ 
    cellsize = (Map->scale/MS_PPI)/inchesPerUnit[Map->units];
    Map->extent.minx = MapPnt.x - cellsize*Map->width/2.0;
    Map->extent.miny = MapPnt.y - cellsize*Map->height/2.0;
    Map->extent.maxx = MapPnt.x + cellsize*Map->width/2.0;
    Map->extent.maxy = MapPnt.y + cellsize*Map->height/2.0;
    break;
  default: /* use the default in the mapfile if it exists */
    if((Map->extent.minx == Map->extent.maxx) && (Map->extent.miny == Map->extent.maxy)) {
      msSetError(MS_WEBERR, "No way to generate map extent.", "mapserv()");
      writeError();
    }    
  }

  RawExt = Map->extent; /* save unaltered extent */
}

void setExtentFromShapes() {
  int i;
  int found=0;
  double dx, dy;
  layerObj *lp;

  for(i=0; i<Map->numlayers; i++) {
    lp = &(Map->layers[i]);

    if(!lp->resultcache) continue;
    if(lp->resultcache->numresults <= 0) continue;

    if(!found) {
      Map->extent = lp->resultcache->bounds;
      found = 1;
    } else
      msMergeRect(&(Map->extent), &(lp->resultcache->bounds));
  }

  dx = Map->extent.maxx - Map->extent.minx;
  dy = Map->extent.maxy - Map->extent.miny;
 
  MapPnt.x = dx/2;
  MapPnt.y = dy/2;
  Map->extent.minx -= dx*EXTENT_PADDING/2.0;
  Map->extent.maxx += dx*EXTENT_PADDING/2.0;
  Map->extent.miny -= dy*EXTENT_PADDING/2.0;
  Map->extent.maxy += dy*EXTENT_PADDING/2.0;

  RawExt = Map->extent; // save unadjusted extent

  /* FIX: NEED TO ADD A COUPLE OF POINT BASED METHODS BASED ON SCALE or BUFFER*/

  return;
}

/* FIX: NEED ERROR CHECKING HERE FOR IMGPNT or MAPPNT */
void setCoordinate()
{
  double cellx,celly;

  cellx = (ImgExt.maxx - ImgExt.minx)/(ImgCols-1);
  celly = (ImgExt.maxy - ImgExt.miny)/(ImgRows-1);

  MapPnt.x = ImgExt.minx + cellx*ImgPnt.x;
  MapPnt.y = ImgExt.maxy - celly*ImgPnt.y;

  return;
}

void returnCoordinate()
{
  msSetError(MS_NOERR, NULL, NULL);
  sprintf(ms_error.message, "Your \"<i>click</i>\" corresponds to (approximately): (%g, %g).\n", MapPnt.x, MapPnt.y);

#ifdef USE_PROJ
  if(Map->projection.proj != NULL) {
    pointObj p=MapPnt;
    msProjectPoint(Map->projection.proj, NULL, &p);
    sprintf(ms_error.message, "%s Computed lat/lon value is (%g, %g).\n", ms_error.message, p.x, p.y);
  }
#endif

  writeError();
}

char *processLine(char *instr, int mode) 
{
  int i;
  char repstr[1024], substr[1024], *outstr; // repstr = replace string, substr = sub string

#ifdef USE_PROJ
  rectObj llextent;
  pointObj llpoint;
#endif

  outstr = strdup(instr); // work from a copy
  
  sprintf(repstr, "%s%s%s.%s", Map->web.imageurl, Map->name, Id, outputImageType[Map->imagetype]);
  outstr = gsub(outstr, "[img]", repstr);
  sprintf(repstr, "%s%sref%s.%s", Map->web.imageurl, Map->name, Id, outputImageType[Map->imagetype]);
  outstr = gsub(outstr, "[ref]", repstr);
  sprintf(repstr, "%s%sleg%s.%s", Map->web.imageurl, Map->name, Id, outputImageType[Map->imagetype]);
  outstr = gsub(outstr, "[legend]", repstr);
  sprintf(repstr, "%s%ssb%s.%s", Map->web.imageurl, Map->name, Id, outputImageType[Map->imagetype]);
  outstr = gsub(outstr, "[scalebar]", repstr);

  if(SaveQuery) {
    sprintf(repstr, "%s%s%s.qf", Map->web.imagepath, Map->name, Id);
    outstr = gsub(outstr, "[queryfile]", repstr);
  }
  
  if(SaveMap) {
    sprintf(repstr, "%s%s%s.map", Map->web.imagepath, Map->name, Id);
    outstr = gsub(outstr, "[map]", repstr);
  }

  sprintf(repstr, "%s", getenv("HTTP_HOST")); 
  outstr = gsub(outstr, "[host]", repstr);
  sprintf(repstr, "%s", getenv("SERVER_PORT"));
  outstr = gsub(outstr, "[port]", repstr);
  
  sprintf(repstr, "%s", Id);
  outstr = gsub(outstr, "[id]", repstr);
  
  strcpy(repstr, ""); // Layer list for a "GET" request
  for(i=0;i<NumLayers;i++)    
    sprintf(repstr, "%s&layer=%s", repstr, Layers[i]);
  outstr = gsub(outstr, "[get_layers]", repstr);
  
  strcpy(repstr, ""); // Layer list for a "POST" request
  for(i=0;i<NumLayers;i++)
    sprintf(repstr, "%s%s ", repstr, Layers[i]);
  trimBlanks(repstr);
  outstr = gsub(outstr, "[layers]", repstr);
  outstr = gsub(outstr, "[layers_esc]", (char *)encode_url(repstr));   

  // HERE: NEED TO FINISH ESCAPING

  for(i=0;i<Map->numlayers;i++) { // Layer legend and description strings
    sprintf(repstr, "[%s_desc]", Map->layers[i].name);
    if(Map->layers[i].status == MS_ON)
      outstr = gsub(outstr, repstr, Map->layers[i].description);
    else
      outstr = gsub(outstr, repstr, "");
    
    sprintf(repstr, "[%s_leg]", Map->layers[i].name);
    if(Map->layers[i].status == MS_ON)
      outstr = gsub(outstr, repstr, Map->layers[i].legend);
    else
      outstr = gsub(outstr, repstr, "");
  }

  for(i=0;i<Map->numlayers;i++) { // Set form widgets (i.e. checkboxes, radio and select lists), note that default layers don't show up here
    if(isOn(Map->layers[i].name, Map->layers[i].group) == MS_TRUE) {
      if(Map->layers[i].group) {
	sprintf(repstr, "[%s_select]", Map->layers[i].group);
	outstr = gsub(outstr, repstr, "selected");
	sprintf(repstr, "[%s_check]", Map->layers[i].group);
	outstr = gsub(outstr, repstr, "checked");
      }
      sprintf(repstr, "[%s_select]", Map->layers[i].name);
      outstr = gsub(outstr, repstr, "selected");
      sprintf(repstr, "[%s_check]", Map->layers[i].name);
      outstr = gsub(outstr, repstr, "checked");
    } else {
      if(Map->layers[i].group) {
	sprintf(repstr, "[%s_select]", Map->layers[i].group);
	outstr = gsub(outstr, repstr, "");
	sprintf(repstr, "[%s_check]", Map->layers[i].group);
	outstr = gsub(outstr, repstr, "");
      }
      sprintf(repstr, "[%s_select]", Map->layers[i].name);
      outstr = gsub(outstr, repstr, "");
      sprintf(repstr, "[%s_check]", Map->layers[i].name);
      outstr = gsub(outstr, repstr, "");
    }
  }

  for(i=-1;i<=1;i++) { /* make zoom direction persistant */
    if(ZoomDirection == i) {
      sprintf(repstr, "[zoomdir_%d_select]", i);
      outstr = gsub(outstr, repstr, "selected");
      sprintf(repstr, "[zoomdir_%d_check]", i);
      outstr = gsub(outstr, repstr, "checked");
    } else {
      sprintf(repstr, "[zoomdir_%d_select]", i);
      outstr = gsub(outstr, repstr, "");
      sprintf(repstr, "[zoomdir_%d_check]", i);
      outstr = gsub(outstr, repstr, "");
    }
  }
  
  for(i=MINZOOM;i<=MAXZOOM;i++) { /* make zoom persistant */
    if(Zoom == i) {
      sprintf(repstr, "[zoom_%d_select]", i);
      outstr = gsub(outstr, repstr, "selected");
      sprintf(repstr, "[zoom_%d_check]", i);
      outstr = gsub(outstr, repstr, "checked");
    } else {
      sprintf(repstr, "[zoom_%d_select]", i);
      outstr = gsub(outstr, repstr, "");
      sprintf(repstr, "[zoom_%d_check]", i);
      outstr = gsub(outstr, repstr, "");
    }
  }

  sprintf(repstr, "%f", MapPnt.x);
  outstr = gsub(outstr, "[mapx]", repstr);
  sprintf(repstr, "%f", MapPnt.y);
  outstr = gsub(outstr, "[mapy]", repstr);
  
  sprintf(repstr, "%f", Map->extent.minx); // Individual mapextent elements for spatial query building 
  outstr = gsub(outstr, "[minx]", repstr);
  sprintf(repstr, "%f", Map->extent.maxx);
  outstr = gsub(outstr, "[maxx]", repstr);
  sprintf(repstr, "%f", Map->extent.miny);
  outstr = gsub(outstr, "[miny]", repstr);
  sprintf(repstr, "%f", Map->extent.maxy);
  outstr = gsub(outstr, "[maxy]", repstr);
  sprintf(repstr, "%f %f %f %f", Map->extent.minx, Map->extent.miny,  Map->extent.maxx, Map->extent.maxy);
  outstr = gsub(outstr, "[mapext]", repstr);
  outstr = gsub(outstr, "[mapext_esc]", (char *)encode_url(repstr));
  
  sprintf(repstr, "%f", RawExt.minx); // Individual raw extent elements for spatial query building
  outstr = gsub(outstr, "[rawminx]", repstr);
  sprintf(repstr, "%f", RawExt.maxx);
  outstr = gsub(outstr, "[rawmaxx]", repstr);
  sprintf(repstr, "%f", RawExt.miny);
  outstr = gsub(outstr, "[rawminy]", repstr);
  sprintf(repstr, "%f", RawExt.maxy);
  outstr = gsub(outstr, "[rawmaxy]", repstr);
  sprintf(repstr, "%f %f %f %f", RawExt.minx, RawExt.miny,  RawExt.maxx, RawExt.maxy);
  outstr = gsub(outstr, "[rawext]", repstr);
  outstr = gsub(outstr, "[rawext_esc]", (char *)encode_url(repstr)); 
    
#ifdef USE_PROJ
  if((strstr(outstr, "lat]") || strstr(outstr, "lon]")) && Map->projection.proj != NULL) {
    llextent=Map->extent;
    llpoint=MapPnt;
    msProjectRect(Map->projection.proj, NULL, &llextent);
    msProjectPoint(Map->projection.proj, NULL, &llpoint);

    sprintf(repstr, "%f", llpoint.x);
    outstr = gsub(outstr, "[maplon]", repstr);
    sprintf(repstr, "%f", llpoint.y);
    outstr = gsub(outstr, "[maplat]", repstr);
    
    sprintf(repstr, "%f", llextent.minx); /* map extent as lat/lon */
    outstr = gsub(outstr, "[minlon]", repstr);
    sprintf(repstr, "%f", llextent.maxx);
    outstr = gsub(outstr, "[maxlon]", repstr);
    sprintf(repstr, "%f", llextent.miny);
    outstr = gsub(outstr, "[minlat]", repstr);
    sprintf(repstr, "%f", llextent.maxy);
    outstr = gsub(outstr, "[maxlat]", repstr);    
    sprintf(repstr, "%f %f %f %f", llextent.minx, llextent.miny,  llextent.maxx, llextent.maxy);
    outstr = gsub(outstr, "[mapext_latlon]", repstr);
    outstr = gsub(outstr, "[mapext_latlon_esc]", (char *)encode_url(repstr)); 
  }
#endif

  sprintf(repstr, "%d %d", Map->width, Map->height);
  outstr = gsub(outstr, "[mapsize]", repstr);
  outstr = gsub(outstr, "[mapsize]", (char *)encode_url(repstr));

  sprintf(repstr, "%d", Map->width);
  outstr = gsub(outstr, "[mapwidth]", repstr);
  sprintf(repstr, "%d", Map->height);
  outstr = gsub(outstr, "[mapheight]", repstr);
  
  sprintf(repstr, "%f", Map->scale);
  outstr = gsub(outstr, "[scale]", repstr);
  
  sprintf(repstr, "%.1f %.1f", (Map->width-1)/2.0, (Map->height-1)/2.0);
  outstr = gsub(outstr, "[center]", repstr);
  sprintf(repstr, "%.1f", (Map->width-1)/2.0);
  outstr = gsub(outstr, "[center_x]", repstr);
  sprintf(repstr, "%.1f", (Map->height-1)/2.0);
  outstr = gsub(outstr, "[center_y]", repstr);      

  // These are really for situations with multiple result sets only, but often used in header/footer  
  sprintf(repstr, "%d", NR); // total number of results
  outstr = gsub(outstr, "[nr]", repstr);  
  sprintf(repstr, "%d", NL); // total number of layers with results
  outstr = gsub(outstr, "[nl]", repstr);

  if(ResultLayer) {
    sprintf(repstr, "%d", NLR); // total number of results within this layer
    outstr = gsub(outstr, "[nlr]", repstr);
    sprintf(repstr, "%d", RN); // sequential (eg. 1..n) result number within all layers
    outstr = gsub(outstr, "[rn]", repstr);
    sprintf(repstr, "%d", LRN); // sequential (eg. 1..n) result number within this layer
    outstr = gsub(outstr, "[lrn]", repstr);
    outstr = gsub(outstr, "[cl]", ResultLayer->name); // current layer name    
    if(ResultLayer->description) outstr = gsub(outstr, "[cd]", ResultLayer->description); // current layer description
  }

  if(mode == QUERY) { // return shape and/or attributes	
    
    sprintf(repstr, "%f %f", (ResultShape.bounds.maxx+ResultShape.bounds.minx)/2, (ResultShape.bounds.maxy+ResultShape.bounds.miny)/2); 
    outstr = gsub(outstr, "[shpmid]", repstr);
    sprintf(repstr, "%f", (ResultShape.bounds.maxx+ResultShape.bounds.minx)/2);
    outstr = gsub(outstr, "[shpmidx]", repstr);
    sprintf(repstr, "%f", (ResultShape.bounds.maxy+ResultShape.bounds.miny)/2);
    outstr = gsub(outstr, "[shpmidy]", repstr);
    
    sprintf(repstr, "%f %f %f %f", ResultShape.bounds.minx, ResultShape.bounds.miny,  ResultShape.bounds.maxx, ResultShape.bounds.maxy);
    outstr = gsub(outstr, "[shpext]", repstr);
    sprintf(repstr, "%f", ResultShape.bounds.minx);
    outstr = gsub(outstr, "[shpminx]", repstr);
    sprintf(repstr, "%f", ResultShape.bounds.miny);
    outstr = gsub(outstr, "[shpminy]", repstr);
    sprintf(repstr, "%f", ResultShape.bounds.maxx);
    outstr = gsub(outstr, "[shpmaxx]", repstr);
    sprintf(repstr, "%f", ResultShape.bounds.maxy);
    outstr = gsub(outstr, "[shpmaxy]", repstr);
    
    sprintf(repstr, "%d", ResultShape.index);
    outstr = gsub(outstr, "[shpidx]", repstr);
    sprintf(repstr, "%d", ResultShape.tileindex);
    outstr = gsub(outstr, "[tileidx]", repstr);  

    for(i=0;i<ResultLayer->numitems;i++) {	 
      sprintf(substr, "[%s]", ResultLayer->items[i]);
      if(strstr(outstr, substr) != NULL)
	outstr = gsub(outstr, substr, ResultShape.attributes[i]);
      sprintf(substr, "[%s_esc]", ResultLayer->items[i]);
      if(strstr(outstr, substr) != NULL)
	outstr = gsub(outstr, substr, (char *)encode_url(ResultShape.attributes[i]));
    }
    
    // FIX: need to re-incorporate JOINS at some point
  }
  
  for(i=0;i<NumEntries;i++) { 
    sprintf(substr, "[%s]", Entries[i].name);
    outstr = gsub(outstr, substr, Entries[i].val);
    sprintf(substr, "[%s_esc]", Entries[i].name);
    outstr = gsub(outstr, substr, (char *)encode_url(Entries[i].val));    
  }

  return(outstr);
}

void returnPage(char *html, int mode)
{
  FILE *stream;
  char line[MS_BUFFER_LENGTH], *tmpline;

  regex_t re; /* compiled regular expression to be matched */ 

  fprintf(stderr, "processing %s\n", html);

  if(regcomp(&re, MS_TEMPLATE_EXPR, REG_EXTENDED|REG_NOSUB) != 0) {   
    msSetError(MS_REGEXERR, NULL, "returnPage()");
    writeError();
  }

  if(regexec(&re, html, 0, NULL, 0) != 0) { /* no match */
    regfree(&re);
    msSetError(MS_WEBERR, "Malformed template name.", "returnPage()");
    writeError();
  }
  regfree(&re);

  if((stream = fopen(html, "r")) == NULL) {
    msSetError(MS_IOERR, html, "returnPage()");
    writeError();
  } 

  while(fgets(line, MS_BUFFER_LENGTH, stream) != NULL) { /* now on to the end of the file */

    if(strchr(line, '[') != NULL) {
      tmpline = processLine(line, mode);
      printf("%s", tmpline);
      free(tmpline);
    } else
      printf("%s", line);

   fflush(stdout);
  } // next line

  fclose(stream);
}

void returnURL(char *url, int mode)
{
  char *tmpurl;

  if(url == NULL) {
    msSetError(MS_WEBERR, "Empty URL.", "returnURL()");
    writeError();
  }

  tmpurl = processLine(url, mode);
  redirect(tmpurl);
  free(tmpurl);
}

// FIX: need to consider JOINS
// FIX: need to consider 5% shape extent expansion

void returnQuery()
{
  int status;
  int i,j;

  layerObj *lp=NULL;

  msInitShape(&ResultShape); // ResultShape is a global var define in mapserv.h

  if((Mode == ITEMQUERY) || (Mode == QUERY)) { // may need to handle a URL result set

    for(i=(Map->numlayers-1); i>=0; i--) {
      lp = &(Map->layers[i]);

      if(!lp->resultcache) continue;
      if(lp->resultcache->numresults > 0) break;
    }

    if(TEMPLATE_TYPE(lp->class[(int)(lp->resultcache->results[0].classindex)].template) == MS_URL) {
      ResultLayer = lp;

      status = msLayerOpen(lp, Map->shapepath);
      if(status != MS_SUCCESS) writeError();

      if(lp->items) { // FIX: may be a better way to do this
	msFreeCharArray(lp->items, lp->numitems);
	lp->numitems = 0;
      }

      status = msLayerGetShape(lp, Map->shapepath, &ResultShape, lp->resultcache->results[0].tileindex, lp->resultcache->results[0].shapeindex, MS_TRUE);
      if(status != MS_SUCCESS) writeError();

      returnURL(lp->class[(int)(lp->resultcache->results[0].classindex)].template, QUERY);      
      
      msFreeShape(&ResultShape);
      msLayerClose(lp);
      ResultLayer = NULL;

      return;
    }
  }

  fprintf(stderr, "here: starting to return query\n");

  NR = NL = 0;
  for(i=0; i<Map->numlayers; i++) { // compute some totals
    lp = &(Map->layers[i]);

    if(!lp->resultcache) break;

    NL++;
    NR += lp->resultcache->numresults;
  }

  printf("Content-type: text/html%c%c", 10, 10); // write MIME header
  printf("<!-- %s -->\n", msGetVersion());
  fflush(stdout);
  
  if(Map->web.header) returnPage(Map->web.header, BROWSE);

  RN = 1; // overall result number
  for(i=(Map->numlayers-1); i>=0; i--) {
    ResultLayer = lp = &(Map->layers[i]);

    if(!lp->resultcache) continue;
    if(lp->resultcache->numresults <= 0) continue;

    NLR = lp->resultcache->numresults; 

    if(lp->header) returnPage(lp->header, BROWSE);

    // open this layer
    status = msLayerOpen(lp, Map->shapepath);
    if(status != MS_SUCCESS) writeError();

    LRN = 1; // layer result number
    for(j=0; j<lp->resultcache->numresults; j++) {
      status = msLayerGetShape(lp, Map->shapepath, &ResultShape, lp->resultcache->results[j].tileindex, lp->resultcache->results[j].shapeindex, MS_TRUE);
      if(status != MS_SUCCESS) writeError();

      returnPage(lp->class[(int)(lp->resultcache->results[j].classindex)].template, QUERY);      

      msFreeShape(&ResultShape); // init too

      RN++; // increment counters
      LRN++;
    }

    if(lp->footer) returnPage(lp->footer, BROWSE);

    msLayerClose(lp);
    ResultLayer = NULL;
  }

  if(Map->web.footer) returnPage(Map->web.footer, BROWSE);

  return;
}

/*
**
** Start of main program
**
*/
int main(int argc, char *argv[]) {
    int i,j;
    char buffer[1024];
    gdImagePtr img=NULL;
    int status;

#ifdef USE_EGIS
    // OV -egis- Initialize egis error log file here...
    initErrLog("/export/home/tmp/msError.log");
    writeErrLog("ErrorLogfile is initialized\n");
    fflush(fpErrLog);
#endif

    if(argc > 1 && strcmp(argv[1], "-v") == 0) {
      printf("%s\n", msGetVersion());
      fflush(stdout);
      exit(0);
    }
    else if (argc > 1 && strncmp(argv[1], "QUERY_STRING=", 13) == 0) {
      /* Debugging hook... pass "QUERY_STRING=..." on the command-line */
      char *buf;
      buf = strdup("REQUEST_METHOD=GET");
      putenv(buf);
      buf = strdup(argv[1]);
      putenv(buf);
    }

    sprintf(Id, "%ld%d",(long)time(NULL),(int)getpid()); // asign now so it can be overridden

    NumEntries = loadEntries(Entries);
    loadForm();
 
    if(SaveMap) {
      sprintf(buffer, "%s%s%s.map", Map->web.imagepath, Map->name, Id);
      if(msSaveMap(Map, buffer) == -1) writeError();
    }

    if((CoordSource == FROMIMGPNT) || (CoordSource == FROMIMGBOX)) /* make sure extent of existing image matches shape of image */
      Map->cellsize = msAdjustExtent(&ImgExt, ImgCols, ImgRows);

    /*
    ** For each layer lets set layer status
    */
    for(i=0;i<Map->numlayers;i++) {
      if((Map->layers[i].status != MS_DEFAULT) && (Map->layers[i].status != MS_QUERY)) {
	if(isOn(Map->layers[i].name, Map->layers[i].group) == MS_TRUE) /* Set layer status */
	  Map->layers[i].status = MS_ON;
	else
	  Map->layers[i].status = MS_OFF;
      }     
    }

    if(CoordSource == FROMREFPNT) /* force browse mode if the reference coords are set */
      Mode = BROWSE;

    if(Mode == BROWSE) {

      if(!Map->web.template) {
	msSetError(MS_WEBERR, "No template provided.", "mapserv()");
	writeError();
      }

      if(QueryFile) {
	status = msLoadQuery(Map, QueryFile);
	if(status != MS_SUCCESS) writeError();
      }
      
      setExtent();      
      Map->cellsize = msAdjustExtent(&(Map->extent), Map->width, Map->height);      
      Map->scale = msCalculateScale(Map->extent, Map->units, Map->width, Map->height);

      if((Map->scale < Map->web.minscale) && (Map->web.minscale > 0)) {
	if(Map->web.mintemplate) { // use the template provided
	  if(TEMPLATE_TYPE(Map->web.mintemplate) == MS_FILE)
	    returnPage(Map->web.mintemplate, BROWSE);
	  else
	    returnURL(Map->web.mintemplate, BROWSE);
	} else { /* force zoom = 1 (i.e. pan) */
	  fZoom = Zoom = 1;
	  ZoomDirection = 0;
	  CoordSource = FROMSCALE;
	  Map->scale = Map->web.minscale;
	  MapPnt.x = (Map->extent.maxx + Map->extent.minx)/2; // use center of bad extent
	  MapPnt.y = (Map->extent.maxy + Map->extent.miny)/2;
	  setExtent();
	  Map->cellsize = msAdjustExtent(&(Map->extent), Map->width, Map->height);      
	  Map->scale = msCalculateScale(Map->extent, Map->units, Map->width, Map->height);
	}
      }
      if((Map->scale > Map->web.maxscale) && (Map->web.maxscale > 0)) {
	if(Map->web.maxtemplate) { // use the template provided
	  if(TEMPLATE_TYPE(Map->web.maxtemplate) == MS_FILE)
	    returnPage(Map->web.maxtemplate, BROWSE);
	  else
	    returnURL(Map->web.maxtemplate, BROWSE);
	} else { /* force zoom = 1 (i.e. pan) */
	  fZoom = Zoom = 1;
	  ZoomDirection = 0;
	  CoordSource = FROMSCALE;
	  Map->scale = Map->web.maxscale;
	  MapPnt.x = (Map->extent.maxx + Map->extent.minx)/2; // use center of bad extent
	  MapPnt.y = (Map->extent.maxy + Map->extent.miny)/2;
	  setExtent();
	  Map->cellsize = msAdjustExtent(&(Map->extent), Map->width, Map->height);
	  Map->scale = msCalculateScale(Map->extent, Map->units, Map->width, Map->height);
	}
      }
   
      if(Map->status == MS_ON) {
	if(QueryFile)
	  img = msDrawQueryMap(Map);
	else
	  img = msDrawMap(Map);
	if(!img) writeError();
	sprintf(buffer, "%s%s%s.%s", Map->web.imagepath, Map->name, Id, outputImageType[Map->imagetype]);	
#ifndef USE_GD_1_8
	if(msSaveImage(img, buffer, Map->transparent, Map->interlace) == -1) writeError();
#else
	if(msSaveImage(img, buffer, Map->imagetype, Map->transparent, Map->interlace, Map->imagequality) == -1) writeError();
#endif
	gdImageDestroy(img);
      }
      
      if(Map->legend.status == MS_ON) {
	img = msDrawLegend(Map);
	if(!img) writeError();
	sprintf(buffer, "%s%sleg%s.%s", Map->web.imagepath, Map->name, Id, outputImageType[Map->imagetype]);
#ifndef USE_GD_1_8
	if(msSaveImage(img, buffer, Map->legend.transparent, Map->legend.interlace) == -1) writeError();
#else
	if(msSaveImage(img, buffer, Map->imagetype, Map->legend.transparent, Map->legend.interlace, Map->imagequality) == -1) writeError();
#endif
	gdImageDestroy(img);
      }
      
      if(Map->scalebar.status == MS_ON) {
	img = msDrawScalebar(Map);
	if(!img) writeError();
	sprintf(buffer, "%s%ssb%s.%s", Map->web.imagepath, Map->name, Id, outputImageType[Map->imagetype]);
#ifndef USE_GD_1_8
	if(msSaveImage(img, buffer, Map->scalebar.transparent, Map->scalebar.interlace) == -1) writeError();
#else
	if(msSaveImage(img, buffer, Map->imagetype, Map->scalebar.transparent, Map->scalebar.interlace, Map->imagequality) == -1) writeError();
#endif
	gdImageDestroy(img);
      }

      if(Map->reference.status == MS_ON) {
	img = msDrawReferenceMap(Map);
	if(!img) writeError();
	sprintf(buffer, "%s%sref%s.%s", Map->web.imagepath, Map->name, Id, outputImageType[Map->imagetype]);
#ifndef USE_GD_1_8
	if(msSaveImage(img, buffer, Map->transparent, Map->interlace) == -1) writeError();
#else
	if(msSaveImage(img, buffer, Map->imagetype, Map->transparent, Map->interlace, Map->imagequality) == -1) writeError();
#endif
	gdImageDestroy(img);
      }

      if(QueryFile) {
	returnQuery();
      } else {
	if(TEMPLATE_TYPE(Map->web.template) == MS_FILE) { /* if thers's an html template, then use it */
	  printf("Content-type: text/html%c%c", 10, 10); /* write MIME header */
	  printf("<!-- %s -->\n", msGetVersion());
	  fflush(stdout);
	  returnPage(Map->web.template, BROWSE);
	} else {	
	  returnURL(Map->web.template, BROWSE);
	} 
      }

    } else if(Mode == MAP || Mode == SCALEBAR || Mode == LEGEND || Mode == REFERENCE) { // "image" only modes
      setExtent();
	
      switch(Mode) {
      case MAP:
	if(QueryFile) {
	  status = msLoadQuery(Map, QueryFile);
	  if(status != MS_SUCCESS) writeError();
	  img = msDrawQueryMap(Map);
	} else
	  img = msDrawMap(Map);
	break;
      case REFERENCE:      
	Map->cellsize = msAdjustExtent(&(Map->extent), Map->width, Map->height);
	img = msDrawReferenceMap(Map);
	break;      
      case SCALEBAR:
	img = msDrawScalebar(Map);
	break;
      case LEGEND:
	img = msDrawLegend(Map);
	break;
      }
      
      if(!img) writeError();
      
      printf("Content-type: image/%s%c%c", outputImageType[Map->imagetype], 10,10);
#ifndef USE_GD_1_8
      status = msSaveImage(img, NULL, Map->transparent, Map->interlace);
#else
      status = msSaveImage(img, NULL, Map->imagetype, Map->transparent, Map->interlace, Map->imagequality);
#endif
      if(status != MS_SUCCESS) writeError();
      
      gdImageDestroy(img);
    } else if(Mode >= QUERY) { // query modes

      if((QueryLayerIndex = msGetLayerIndex(Map, QueryLayer)) != -1) /* force the query layer on */
	Map->layers[QueryLayerIndex].status = MS_ON;

      switch(Mode) {
      case FEATUREQUERY:
      case FEATURENQUERY:
	if((SelectLayerIndex = msGetLayerIndex(Map, SelectLayer)) == -1) { /* force the selection layer on */
	  msSetError(MS_WEBERR, "Selection layer not set or references an invalid layer.", "mapserv()"); 
	  writeError();
	} else
	  Map->layers[SelectLayerIndex].status = MS_ON;

	if(QueryCoordSource == NONE) { // use attributes

	  if((status = msQueryByAttributes(Map, SelectLayerIndex)) != MS_SUCCESS) writeError();	  

	} else { /* use coordinates */
	  
	  if(Mode == FEATUREQUERY) {
	    switch(QueryCoordSource) {
	    case FROMIMGPNT:
	      Map->extent = ImgExt; /* use the existing map extent */	
	      setCoordinate();
	      if((status = msQueryByPoint(Map, SelectLayerIndex, MS_SINGLE, MapPnt, 0)) != MS_SUCCESS) writeError();
	      break;
	    case FROMUSERPNT: /* only a buffer makes sense */
	      if(Buffer == -1) {
		msSetError(MS_WEBERR, "Point given but no search buffer specified.", "mapserv()");
		writeError();
	      }
	      if((status = msQueryByPoint(Map, SelectLayerIndex, MS_SINGLE, MapPnt, Buffer)) != MS_SUCCESS) writeError();
	      break;
	    default:
	      msSetError(MS_WEBERR, "No way to the initial search, not enough information.", "mapserv()");
	      writeError();
	      break;
	    }	  
	  } else { /* FEATURENQUERY */
	    switch(QueryCoordSource) {
	    case FROMIMGPNT:
	      Map->extent = ImgExt; /* use the existing map extent */	
	      setCoordinate();
	      if((status = msQueryByPoint(Map, SelectLayerIndex, MS_MULTIPLE, MapPnt, 0)) != MS_SUCCESS) writeError();
	      break;	 
	    case FROMIMGBOX:
	      break;
	    case FROMUSERPNT: /* only a buffer makes sense */
	      if((status = msQueryByPoint(Map, SelectLayerIndex, MS_MULTIPLE, MapPnt, Buffer)) != MS_SUCCESS) writeError();
	    default:
	      setExtent();
	      if((status = msQueryByRect(Map, SelectLayerIndex, Map->extent)) != MS_SUCCESS) writeError();
	      break;
	    }
	  } /* end switch */
	} 
	
	if(msQueryByFeatures(Map, QueryLayerIndex, SelectLayerIndex) == -1) writeError();
      
	break;
      case ITEMQUERY:
      case ITEMQUERYMAP:
	
	if(QueryCoordSource != NONE && !UseShapes)
	  setExtent(); /* set user area of interest */

	if((status = msQueryByAttributes(Map, QueryLayerIndex)) != MS_SUCCESS) writeError();

	break;
      case NQUERY:
      case NQUERYMAP:
	switch(QueryCoordSource) {
	case FROMIMGPNT:	  
	  setCoordinate();
	  
	  if(SearchMap) { // compute new extent, pan etc then search that extent
	    setExtent();
	    Map->cellsize = msAdjustExtent(&(Map->extent), Map->width, Map->height);
	    Map->scale = msCalculateScale(Map->extent, Map->units, Map->width, Map->height);
	    if((status = msQueryByRect(Map, QueryLayerIndex, Map->extent)) != MS_SUCCESS) writeError();
	  } else {
	    Map->extent = ImgExt; // use the existing image parameters
	    Map->width = ImgCols;
	    Map->height = ImgRows;
	    Map->scale = msCalculateScale(Map->extent, Map->units, Map->width, Map->height);	 
	    if((status = msQueryByPoint(Map, QueryLayerIndex, MS_MULTIPLE, MapPnt, 0)) != MS_SUCCESS) writeError();
	  }
	  break;	  
	case FROMIMGBOX:	  
	  if(SearchMap) { // compute new extent, pan etc then search that extent
	    setExtent();
	    Map->scale = msCalculateScale(Map->extent, Map->units, Map->width, Map->height);
	    Map->cellsize = msAdjustExtent(&(Map->extent), Map->width, Map->height);
	    if((status = msQueryByRect(Map, QueryLayerIndex, Map->extent)) != MS_SUCCESS) writeError();
	  } else {
	    double cellx, celly;
	    
	    Map->extent = ImgExt; // use the existing image parameters
	    Map->width = ImgCols;
	    Map->height = ImgRows;
	    Map->scale = msCalculateScale(Map->extent, Map->units, Map->width, Map->height);	  
	    
	    cellx = (ImgExt.maxx-ImgExt.minx)/(ImgCols-1); // calculate the new search extent
	    celly = (ImgExt.maxy-ImgExt.miny)/(ImgRows-1);
	    RawExt.minx = ImgExt.minx + cellx*ImgBox.minx;
	    RawExt.maxx = ImgExt.minx + cellx*ImgBox.maxx;
	    RawExt.miny = ImgExt.maxy - celly*ImgBox.maxy;
	    RawExt.maxy = ImgExt.maxy - celly*ImgBox.miny;
	    
	    if((status = msQueryByRect(Map, QueryLayerIndex, RawExt)) != MS_SUCCESS) writeError();
	  }
	  break;
	case FROMIMGSHAPE:
	  Map->extent = ImgExt; // use the existing image parameters
	  Map->width = ImgCols;
	  Map->height = ImgRows;
	  Map->cellsize = msAdjustExtent(&(Map->extent), Map->width, Map->height);
	  Map->scale = msCalculateScale(Map->extent, Map->units, Map->width, Map->height);
	  
	  // convert from image to map coordinates here (see setCoordinate)
	  for(i=0; i<SelectShape.numlines; i++) {
	    for(j=0; j<SelectShape.line[i].numpoints; j++) {
	      SelectShape.line[i].point[j].x = Map->extent.minx + Map->cellsize*SelectShape.line[i].point[j].x;
	      SelectShape.line[i].point[j].y = Map->extent.maxy - Map->cellsize*SelectShape.line[i].point[j].y;
	    }
	  }
	  
	  if((status = msQueryByShape(Map, QueryLayerIndex, &SelectShape)) != MS_SUCCESS) writeError();
	  break;	  
	case FROMUSERPNT:
	  if(Buffer == 0) {
	    if((status = msQueryByPoint(Map, QueryLayerIndex, MS_MULTIPLE, MapPnt, Buffer)) != MS_SUCCESS) writeError();
	    setExtent();
	  } else {
	    setExtent();
	    if((status = msQueryByRect(Map, QueryLayerIndex, Map->extent)) != MS_SUCCESS) writeError();
	  }
	  break;
	case FROMUSERSHAPE:
	  setExtent();
	  if((status = msQueryByShape(Map, QueryLayerIndex, &SelectShape)) != MS_SUCCESS) writeError();
	  break;	  
	default: // from an extent of some sort
	  setExtent();
	  if((status = msQueryByRect(Map, QueryLayerIndex, Map->extent)) != MS_SUCCESS) writeError();
	  break;
	}      
	break;
      case QUERY:
      case QUERYMAP:
	switch(QueryCoordSource) {
	case FROMIMGPNT:
	  fprintf(stderr, "here (1) in mapserv...\n");
	  setCoordinate();
	  Map->extent = ImgExt; // use the existing image parameters
	  Map->width = ImgCols;
	  Map->height = ImgRows;
	  Map->scale = msCalculateScale(Map->extent, Map->units, Map->width, Map->height);	 	  
	  if((status = msQueryByPoint(Map, QueryLayerIndex, MS_SINGLE, MapPnt, 0)) != MS_SUCCESS) writeError();
	  fprintf(stderr, "here (2) in mapserv...\n");
	  break;
	  
	case FROMUSERPNT: /* only a buffer makes sense, DOES IT? */	
	  setExtent();	
	  if((status = msQueryByPoint(Map, QueryLayerIndex, MS_SINGLE, MapPnt, Buffer)) != MS_SUCCESS) writeError();
	  break;
	  
	default:
	  msSetError(MS_WEBERR, "Query mode needs a point, imgxy and mapxy are not set.", "mapserv()");
	  writeError();
	  break;
	}

	break;
      } // end mode switch

      fprintf(stderr, "here (3) in mapserv...\n");
      
      if(UseShapes)
	setExtentFromShapes();

      if(Map->querymap.status) {
	img = msDrawQueryMap(Map);
	if(!img) writeError();
	
	if(Mode == QUERYMAP || Mode == NQUERYMAP || Mode == ITEMQUERYMAP) { // just return the image
	  printf("Content-type: image/%s%c%c", outputImageType[Map->imagetype], 10,10);
#ifndef USE_GD_1_8
	  status = msSaveImage(img, NULL, Map->transparent, Map->interlace);
#else
	  status = msSaveImage(img, NULL, Map->imagetype, Map->transparent, Map->interlace, Map->imagequality);
#endif	  
	  if(status != MS_SUCCESS) writeError();
	  gdImageDestroy(img);
	} else {
	  sprintf(buffer, "%s%s%s.%s", Map->web.imagepath, Map->name, Id, outputImageType[Map->imagetype]);
#ifndef USE_GD_1_8
	  status = msSaveImage(img, buffer, Map->transparent, Map->interlace);
#else
	  status = msSaveImage(img, buffer, Map->imagetype, Map->transparent, Map->interlace, Map->imagequality);
#endif
	  if(status != MS_SUCCESS) writeError();
	  gdImageDestroy(img);
	
	  if(Map->legend.status == MS_ON || UseShapes) {
	    img = msDrawLegend(Map);
	    if(!img) writeError();
	    sprintf(buffer, "%s%sleg%s.%s", Map->web.imagepath, Map->name, Id, outputImageType[Map->imagetype]);
#ifndef USE_GD_1_8
	    status = msSaveImage(img, buffer, Map->legend.transparent, Map->legend.interlace);
#else
	    status = msSaveImage(img, buffer, Map->imagetype, Map->legend.transparent, Map->legend.interlace, Map->imagequality);
#endif
	    if(status != MS_SUCCESS) writeError();
	    gdImageDestroy(img);
	  }
	  
	  if(Map->scalebar.status == MS_ON) {
	    img = msDrawScalebar(Map);
	    if(!img) writeError();
	    sprintf(buffer, "%s%ssb%s.%s", Map->web.imagepath, Map->name, Id, outputImageType[Map->imagetype]);
#ifndef USE_GD_1_8
	    status = msSaveImage(img, buffer, Map->scalebar.transparent, Map->scalebar.interlace);
#else
	    status = msSaveImage(img, buffer, Map->imagetype, Map->scalebar.transparent, Map->scalebar.interlace, Map->imagequality);
#endif
	    if(status != MS_SUCCESS) writeError();
	    gdImageDestroy(img);
	  }
	  
	  if(Map->reference.status == MS_ON) {
	    img = msDrawReferenceMap(Map);
	    if(!img) writeError();
	    sprintf(buffer, "%s%sref%s.%s", Map->web.imagepath, Map->name, Id, outputImageType[Map->imagetype]);
#ifndef USE_GD_1_8
	    status = msSaveImage(img, buffer, Map->transparent, Map->interlace);
#else
	    status = msSaveImage(img, buffer, Map->imagetype, Map->transparent, Map->interlace, Map->imagequality);
#endif
	    if(status != MS_SUCCESS) writeError();
	    gdImageDestroy(img);
	  }
	}
      }
      
      returnQuery();

      if(SaveQuery) {
	sprintf(buffer, "%s%s%s%s", Map->web.imagepath, Map->name, Id, MS_QUERY_EXTENSION);
	if(msSaveQuery(Map, buffer) != MS_SUCCESS) writeError();
      }

    } else if(Mode == COORDINATE) {
      setCoordinate(); // mouse click => map coord
      returnCoordinate(MapPnt);
    } else if(Mode == PROCESSING) {
#ifdef USE_EGIS
      setExtent();
      errLogMsg[0] = '\0';
      sprintf(errLogMsg, "Map Coordinates: x %f, y %f\n", MapPnt.x, MapPnt.y);
      writeErrLog(errLogMsg);
      
      status = egisl(Map, Entries, NumEntries, CoordSource);
      // printf("Numerical Window - %f %f\n", ImgPnt.x, ImgPnt.y);
      fflush(stdout);
      
      if (status == 1) {
	// write MIME header
	printf("Content-type: text/html%c%c", 10, 10);
	returnStatistics("numwin.html");
      } else if(status == 2) {
	writeErrLog("Calling returnSplot()\n");
	fflush(fpErrLog);
	
	// write MIME header
	printf("Content-type: text/html%c%c", 10, 10);
	returnSplot("splot.html");
      } else if(status == 3) {
	writeErrLog("Calling returnHisto()\n");
	fflush(fpErrLog);
	
	// write MIME header
	printf("Content-type: text/html%c%c", 10, 10);
	returnHisto("histo.html");
      } else {
	writeErrLog("Error status from egisl()\n");
	fflush(fpErrLog);
	
	errLogMsg[0] = '\0';
	sprintf(errLogMsg, "Select Image Layer\n");
	
	msSetError(MS_IOERR, "<br>eGIS Error: Choose Stack Image<br>", errLogMsg);
	writeError();
      }
#else
      msSetError(MS_MISCERR, "EGIS Support is not available.", "mapserv()");
      writeError();
#endif
    }

    writeLog(MS_FALSE);
   
    /*
    ** Clean-up
    */
    msFreeMap(Map);

    for(i=0;i<NumEntries;i++) {
      free(Entries[i].val);
      free(Entries[i].name);
    }

    free(Item);
    free(Value);      
    free(QueryLayer);
    free(SelectLayer);
    free(QueryFile);

    for(i=0;i<NumLayers;i++) free(Layers[i]);

    exit(0); // end MapServer
} 
