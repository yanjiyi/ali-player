extern "C" {
#include <GL/glew.h>
#include <SDL2/SDL.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavfilter/avfilter.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include <cstring>
#include <iostream>
#include <vector>

static enum AVPixelFormat get_vaapi_format(AVCodecContext *ctx,
		const enum AVPixelFormat *pix_fmts)
{
	const enum AVPixelFormat *p;

	for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
		if (*p == AV_PIX_FMT_VAAPI)
			return *p;
	}

	fprintf(stderr, "Unable to decode this file using VA-API.\n");
	return AV_PIX_FMT_NONE;
}

static float videoVertices[] = {
	-1.0f,1.0f,0.0f, 1.0f,0.0f,
	1.0f,1.0f,0.0f, 1.0f,1.0f,
	-1.0f,-1.0f,0.0f, 0.0f,0.0f,
	-1.0f,-1.0f,0.0f, 0.0f,0.0f,
	1.0f,-1.0f,0.0f, 0.0f,1.0f,
	1.0f,1.0f,0.0f, 1.0f,1.0f
};

const char* vertexShaderSource = {
	"#version 110\n"
	"attribute vec3 vPos;\n"
	"attribute vec2 vTexCoords;\n"
	"varying vec2 fTexCoords;\n"
	"void main() { \n"
	" 	fTexCoords = vTexCoords;\n"
	" 	gl_Position = vec4(vPos,1.0); }\n"
};

const char* fragmentShaderSource = {
	"#version 110\n"
	"varying vec2 fTexCoords;\n"
	"uniform sampler2D texture;\n"
	"void main() {\n"
	" 	gl_FragColor = texture2D(texture,fTexCoords); }\n"
};

GLuint BuildShader(const char* vSource,const char* fSource)
{
	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader,1,(const GLchar* const*)&vSource,0);
	glCompileShader(vertexShader);

	GLint isCompiled = GL_FALSE;
	glGetShaderiv(vertexShader,GL_COMPILE_STATUS,&isCompiled);
	if(GL_FALSE == isCompiled)
	{
		GLint maxLength = 0;
		glGetShaderiv(vertexShader,GL_INFO_LOG_LENGTH,&maxLength);

		std::vector<GLchar> infoLog(maxLength);
		glGetShaderInfoLog(vertexShader,maxLength,&maxLength,&infoLog[0]);

		fprintf(stderr,"Compile Vertex Shader Error : \n %s\n",(const char*)infoLog.data());

		glDeleteShader(vertexShader);
		return 0;
	}

	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader,1,(const GLchar* const*)&fSource,0);
	glCompileShader(fragmentShader);

	glGetShaderiv(fragmentShader,GL_COMPILE_STATUS,&isCompiled);
	if(GL_FALSE == isCompiled)
	{
		GLint maxLength = 0;
		glGetShaderiv(fragmentShader,GL_INFO_LOG_LENGTH,&maxLength);

		std::vector<GLchar> infoLog(maxLength);
		glGetShaderInfoLog(fragmentShader,maxLength,&maxLength,&infoLog[0]);

		fprintf(stderr,"Compile Fragment Shader Error : \n %s\n",(const char*)infoLog.data());

		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);
		return 0;
	}

	GLuint program = glCreateProgram();

	glAttachShader(program,vertexShader);
	glAttachShader(program,fragmentShader);
	glLinkProgram(program);

	GLint isLinked = GL_FALSE;
	glGetProgramiv(program,GL_LINK_STATUS,&isLinked);
	if(GL_FALSE == isLinked)
	{
		GLint maxLength = 0;
		glGetProgramiv(program,GL_INFO_LOG_LENGTH,&maxLength);

		std::vector<GLchar> infoLog(maxLength);
		glGetProgramInfoLog(program,maxLength,&maxLength,&infoLog[0]);

		fprintf(stderr,"Link Shader Program Error : \n %s\n",(const char*)infoLog.data());
		glDetachShader(program,vertexShader);
		glDetachShader(program,fragmentShader);
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);
		glDeleteProgram(program);

		return 0;
	}

	glDetachShader(program,vertexShader);
	glDetachShader(program,fragmentShader);

	return program;
}

int main(int argc,char *argv[])
{
	AVFormatContext * ifmt_ctx = nullptr;
	AVBufferRef *hw_device_ctx = nullptr;
	AVCodecContext *decoder_ctx = nullptr;
	int video_stream = -1;

	const AVCodec *decoder = nullptr;
	AVStream *video = nullptr;
	AVPacket *dec_pkt;
	int ret = 0;

	bool va_enable = true;

	if(argc==1)
	{
		printf("Usage : media_file \n");
		return ret;
	}

	do {

		ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI, NULL, NULL, 0);
		if (ret < 0) {
			fprintf(stderr, "Failed to create a VAAPI device. Error code: %s\n", av_err2str(ret));
			return -1;
		}

		dec_pkt = av_packet_alloc();
		if (!dec_pkt) {
			fprintf(stderr, "Failed to allocate decode packet\n");
			break;
		}

		if((ret = avformat_open_input(&ifmt_ctx, argv[1], NULL, NULL)) < 0)
		{
			fprintf(stderr, "Cannot open input file '%s', Error code: %s\n",
					argv[1], av_err2str(ret));
			break;
		}

		if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
			fprintf(stderr, "Cannot find input stream information. Error code: %s\n",
					av_err2str(ret));
			break;
		}

		ret = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
		if (ret < 0) {
			fprintf(stderr, "Cannot find a video stream in the input file. "
					"Error code: %s\n", av_err2str(ret));
			break;
		}
		video_stream = ret;

		if (!(decoder_ctx = avcodec_alloc_context3(decoder)))
			break;

		video = ifmt_ctx->streams[video_stream];
		if ((ret = avcodec_parameters_to_context(decoder_ctx, video->codecpar)) < 0) {
			fprintf(stderr, "avcodec_parameters_to_context error. Error code: %s\n",
					av_err2str(ret));
			break;
		}

		decoder_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
		if (!decoder_ctx->hw_device_ctx) {
			fprintf(stderr, "A hardware device reference create failed.\n");
			break;
		}
		decoder_ctx->get_format    = get_vaapi_format;

		if ((ret = avcodec_open2(decoder_ctx, decoder, NULL)) < 0)
		{
			printf("Can not use vaapi , try to use cpu decoder!\n"); 
			va_enable = false;

			if ((ret = avcodec_parameters_to_context(decoder_ctx, video->codecpar)) < 0) 		{
				fprintf(stderr, "avcodec_parameters_to_context error. Error code: %s\n",
						av_err2str(ret));
				break;
			}

			decoder = avcodec_find_decoder(video->codecpar->codec_id);
			if(!decoder)
			{
				fprintf(stderr,"Decoder not found!\n");
				break;
			}

			if((ret = avcodec_open2(decoder_ctx,decoder,NULL)) < 0)
			{
				fprintf(stderr, "Failed to open codec for decoding. Error code: %s\n",
						av_err2str(ret));
				break;
			}
		}

		if(SDL_Init(SDL_INIT_EVERYTHING)<0)
		{
			fprintf(stderr,"SDL Initializtion Error : %s\n",SDL_GetError());
			break;
		}

		SDL_Window * ptrWindow = SDL_CreateWindow("ali-player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_RESIZABLE|SDL_WINDOW_OPENGL|SDL_WINDOW_ALLOW_HIGHDPI);
		if(!ptrWindow)
		{
			fprintf(stderr,"SDL Create Window Error : %s\n",SDL_GetError());
			SDL_Quit();
			break;
		}

		SDL_GLContext glContext = SDL_GL_CreateContext(ptrWindow);
		GLenum error = glewInit();

		if(GLEW_OK!=error)
		{
			fprintf(stderr, "GLEW Initializtion Error : %s\n",(char*)glewGetErrorString(error));
			SDL_DestroyWindow(ptrWindow);
			SDL_Quit();
			break;
		}

		GLuint shader_program = BuildShader(vertexShaderSource,fragmentShaderSource);

		if(shader_program == 0)
			break;

		GLuint vbo;
		glGenBuffers(1,&vbo);
		glBindBuffer(GL_ARRAY_BUFFER,vbo);
		glBufferData(GL_ARRAY_BUFFER,sizeof(videoVertices),videoVertices,GL_STATIC_DRAW);
		glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(float)*5,0);
		glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(float)*5,(void*)(sizeof(float)*3));
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glBindBuffer(GL_ARRAY_BUFFER,0);

		glBindAttribLocation(shader_program,0,"vPos");
		glBindAttribLocation(shader_program,1,"vTexCoords");

		GLuint texture;
		glGenTextures(1,&texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glGenerateMipmap(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D,0);

		SDL_Event event;
		bool run = true;

		Uint64 start = SDL_GetPerformanceCounter();
		while(run)
		{
			Uint64 end = SDL_GetPerformanceCounter();
			float frametime = (end - start) / (float)SDL_GetPerformanceFrequency();

			start = end;

			while(SDL_PollEvent(&event))
			{
				if(event.type == SDL_QUIT)
				{
					run = false;
					break;
				}

				if(event.window.event == SDL_WINDOWEVENT_RESIZED && event.window.windowID == SDL_GetWindowID(ptrWindow))
				{
					int width,height;
					SDL_GetWindowSize(ptrWindow, &width, &height);
					glViewport(0,0,width,height);
				}
			}

			if((ret = av_read_frame(ifmt_ctx,dec_pkt)) < 0)
				break;

			if(video_stream == dec_pkt->stream_index)
			{
				//Do Decode , Update Texture
			}

			av_packet_unref(dec_pkt);

			glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
			glClearColor(0.0f,0.0f,0.0f,1.0f);

			glUseProgram(shader_program);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, texture);
			glBindBuffer(GL_ARRAY_BUFFER,vbo);

			glDrawArrays(GL_TRIANGLES, 0, 6);

			glBindBuffer(GL_ARRAY_BUFFER,0);
			glBindTexture(GL_TEXTURE_2D,0);
			glUseProgram(0);

			SDL_GL_SwapWindow(ptrWindow);
		}

		glDeleteBuffers(1,&vbo);
		glDeleteTextures(1,&texture);
		glDeleteProgram(shader_program);

		SDL_GL_DeleteContext(glContext);
		SDL_DestroyWindow(ptrWindow);
		SDL_Quit();
	}while(0);

	avformat_close_input(&ifmt_ctx);
	avcodec_free_context(&decoder_ctx);
	av_buffer_unref(&hw_device_ctx);
	av_packet_free(&dec_pkt);

	return ret;
}
