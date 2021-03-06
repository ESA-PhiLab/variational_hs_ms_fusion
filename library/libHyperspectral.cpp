#include "libHyperspectral.h"

namespace libUSTGHYPER
{

// NL regularization with different weights for each hyperspectral channel.
// Weight structure: wxy[h][i][n] for each channel h, each pixel i, and each neighbour n.
// L2 fidelity-term forcing closeness to hyperspectral data on the low-resolution domain, defined by low-pass filtering + subsampling.
// L2 fidelity-term forcing closeness to multispectral data on the high-resolution domain, defined by matrix S.
// L1 data constraint.
void hyperfusion_NL_L1_wh(float **u_upd, float **fH, float **fM, float **S, float **St, float **PH, float **PHint, float **fHint,
		std::vector< std::vector< std::vector<float> > > wxy, std::vector< std::vector< std::vector<int> > > posxy,
		std::vector< std::vector< std::vector<float> > > wyx, std::vector< std::vector< std::vector<int> > > posyx,
		std::vector< std::vector< std::vector<int> > > posw, float lmbH, float lmbM, float mu, float tau, float sigma,
		float tol, int maxIter, int sampling, float stdBlur, int hs_channels, int ms_channels, int width, int height)
{
	//u_upd: the unknown image to be found (upd: updated)

	//DEBUGG
	//hs_channels=1;

	// Image size
	int dim = width * height;
	int s_dim = (int) floor((float) dim / (float) (sampling * sampling));

	//size of wxy
	int K = wxy.size(); // number of matrices
	int N = wxy[0].size(); // number of rows for each matrix
	int M = wxy[0][0].size(); // number of lines for each matrix

	// Primal variables
	float **u = new float*[hs_channels];
	float **ubar = new float*[hs_channels];

	for(int h = 0; h < hs_channels; h++)
	{
		u[h] = new float[dim];
		ubar[h] = new float[dim];

		//initializing u_upd, u and ubar with 0.0f
		libUSTG::fpClear(u_upd[h], 0.0f, dim);
		libUSTG::fpClear(u[h], 0.0f, dim);
		libUSTG::fpClear(ubar[h], 0.0f, dim);
	}


	// Dual variables related to NL regularization
	std::vector< std::vector< std::vector <float> > > p;
	std::vector< std::vector< std::vector <float> > > p_upd;
	std::vector< std::vector< std::vector <float> > > p_aux;


	for(int h = 0; h < hs_channels; h++)
	{
		//for each channel h, haux is a 2d vector that contains dim rows
		//and the number of columns of each row is the dimension of the neighbours
		//of the pixel i
		std::vector< std::vector<float> > haux;

		for(int i = 0; i < dim; i++)
		{
			//aux is a 1d vector that
			std::vector<float> aux;
			aux.assign((int) wxy[h][i].size(), 0.0f);

			haux.push_back(aux);
		}

		//p, p_upd a,d p_aux are initialized to 0 at the beginning
		p.push_back(haux);
		p_upd.push_back(haux);
		p_aux.push_back(haux);
	}

	//the divergence matrix it has hs_channels and dim columns
	float **nldiv = new float*[hs_channels];

	for(int h = 0; h < hs_channels; h++)
	{
		nldiv[h] = new float[dim];
		libUSTG::fpClear(nldiv[h], 0.0f, dim);
	}


	// Dual variables realted to hyperspectral fidelity-term
	float **q = new float*[hs_channels];
	float **q_upd = new float*[hs_channels];
	float **q_aux = new float*[hs_channels];

	for(int h = 0; h < hs_channels; h++)
	{
		q[h] = new float[s_dim];
		q_upd[h] = new float[s_dim];
		q_aux[h] = new float[s_dim];

		libUSTG::fpClear(q[h], 0.0f, s_dim);
		libUSTG::fpClear(q_upd[h], 0.0f, s_dim);
		libUSTG::fpClear(q_aux[h], 0.0f, s_dim);
	}


	// Dual variables realted to multispectral fidelity-term
	float **t = new float*[ms_channels];
	float **t_upd = new float*[ms_channels];
	float **t_aux = new float*[ms_channels];

	for(int m = 0; m < ms_channels; m++)
	{
		t[m] = new float[dim];
		t_upd[m] = new float[dim];
		t_aux[m] = new float[dim];

		libUSTG::fpClear(t[m], 0.0f, dim);
		libUSTG::fpClear(t_upd[m], 0.0f, dim);
		libUSTG::fpClear(t_aux[m], 0.0f, dim);
	}


	// Auxiliar variables
	float *aux = new float[dim];
	float *aux2 = new float[dim];
	float *s_aux = new float[s_dim];

	libUSTG::fpClear(aux, 0.0f, dim);
	libUSTG::fpClear(aux2, 0.0f, dim);
	libUSTG::fpClear(s_aux, 0.0f, s_dim);


	// Primal-dual Chambolle-Pock algorithm
	float error = tol;
	int iter = 0;

	int cpt1=0;
	int cpt2=0;
	int cpt3=0;

	while(iter < maxIter && error >= tol)
	{
		
		// Compute proximity operator of dualized NL regularization term
		nl_gradient(ubar, p_aux, wxy, posxy, hs_channels, dim); //here p_aux is the non-local gradient of ubar


		for(int h = 0; h < hs_channels; h++)
			for(int i = 0; i < dim; i++)
				for(int w = 0; w < (int) wxy[h][i].size(); w++)
				{
					//here we compute p^{n}_k+sigma*delta_w(ubar^{n}_{k})
					//std::cout<<" h= "<<h<<" i= "<<i<<std::endl;
					//printf("h is %d i is %d and w is %d \n",h,i,w);
					p_aux[h][i][w] = p[h][i][w] + sigma * p_aux[h][i][w]; //p here is p^{n}_k in the notes
				}

		//here we compute the proximal operator of the non-local regularization and the result is stored
		//in p_upd which corresponds to p^{n+1}_k in the notes
		proxNL(p_upd, p_aux, hs_channels, dim);


		// Compute proximity operator of dualized hyperspectral fidelity term
		for(int h = 0; h < hs_channels; h++)
		{
			libUSTG::fpClear(aux, 0.0f, dim);
			libUSTG::fpClear(s_aux, 0.0f, s_dim);

			//corresponds to the computations of Bubar^{n}_{k}, the result is put in  aux
			FFT_gaussian_convol(aux, ubar[h], stdBlur, width, height);
			//here we compute  DBubar^{n}_{k}
			downsampling(s_aux, aux, sampling, width, height);


			for(int i = 0; i < s_dim; i++)
				//what is computed here is q^{n}_k+sigma*DBubar^{n}_{k}, the result is store in q_aux
				q_aux[h][i] = q[h][i] + sigma * s_aux[i]; //q here is q^{n}_k in the notes
		}
		//what is computed here is the previous result-sigma*f^{h}_{k} divided by (1+sigma/lambda_H)
		proxDualSquaredL2(q_upd, q_aux, fH, lmbH, sigma, hs_channels, s_dim);

		//printf("ms \n");
		// Compute proximity operator of dualized multispetral fidelity term
		for(int m = 0; m < ms_channels; m++)
		{
			for(int i = 0; i < dim; i++)
			{
				float Sval = 0.0f;

				//computing (Subar^{n})_{l}
				for(int h = 0; h < hs_channels; h++)
					Sval += (S[m][h] * ubar[h][i]);

				//Here we compute: t^{n}_{l}+sigma*(Subar^{n})_{l}
				t_aux[m][i] = t[m][i] + sigma * Sval;
			}
		}

		//Here we compute: the previous result-sigma*f^{m}_{l} divided by (1+sigma/lambda_M)
		proxDualSquaredL2(t_upd, t_aux, fM, lmbM, sigma, ms_channels, dim);

		// Compute proximity operator of primal energy term
		nl_divergence(p_upd, nldiv, wxy, wyx, posyx, posw, hs_channels, dim);
		

		for(int h = 0; h < hs_channels; h++)
		{
			libUSTG::fpClear(aux, 0.0f, dim);
			libUSTG::fpClear(aux2, 0.0f, dim);
			libUSTG::fpClear(s_aux, 0.0f, s_dim);

			upsampling_zeropadding(aux, q_upd[h], sampling, width, height);
			FFT_gaussian_convol(aux, aux, stdBlur, width, height);
			//printf("FFT gaussian convol done \n");

			for(int i = 0; i < dim; i++)
			{
				float Stval = 0.0f;

				for(int m = 0; m < ms_channels; m++)
					Stval += (St[h][m] * t_upd[m][i]);

				aux2[i] = Stval;
			}
			//printf("here now \n");

			cpt1=0;
			cpt2=0;
			cpt3=0;
			//La partie correspondante ?? la norme L1
			for(int i = 0; i < dim; i++)
			{
				float Phintval = PHint[h][i];
				float utilde = u[h][i] + tau * (nldiv[h][i] - aux[i] - aux2[i]);

				if(Phintval > fTiny)
				{
					float arg = Phintval * utilde - PH[h][i] * fHint[h][i];
					float thres = tau * mu * Phintval * Phintval;

					if(arg < - thres)
					{
						//u_upd[h][i] = arg + tau * mu * Phintval;
						u_upd[h][i] = utilde + tau * mu * Phintval;
						cpt1=cpt1+1;
					}
					else if(arg > thres)
					{
						//u_upd[h][i] = arg - tau * mu * Phintval;
						u_upd[h][i] = utilde - tau * mu * Phintval;
						cpt2=cpt2+1;
					}

					else
					{
						//u_upd[h][i] = (PH[h][i] * fHint[h][i]) / Phintval;
						u_upd[h][i] =(float) (PH[h][i] * fHint[h][i]) / Phintval;
						cpt3=cpt3+1;
					}

				} else
				{
					u_upd[h][i] = utilde;
				}
			}
		}

		//std::cout<<"cpt1 = "<< cpt1 <<" cpt2 = "<< cpt2<<" cpt3 = "<< cpt3 <<std::endl;

		// Relax primal variable
		for(int h = 0; h < hs_channels; h++)
			for(int i = 0; i < dim; i++)
				ubar[h][i] = 2.0f * u_upd[h][i] - u[h][i];

		// Compute relative error
		error=compute_error(u, u_upd, hs_channels, dim);
		
		//printf("iter = %i; error = %.10f \n", iter, error);

		// Update variables
		for(int h = 0; h < hs_channels; h++)
		{
			libUSTG::fpCopy(u_upd[h], u[h], dim);

			for(int i = 0; i < dim; i++)
				p[h][i].assign(p_upd[h][i].begin(), p_upd[h][i].end());
		}

		for(int h = 0; h < hs_channels; h++)
			libUSTG::fpCopy(q_upd[h], q[h], s_dim);

		for(int m = 0; m < ms_channels; m++)
			libUSTG::fpCopy(t_upd[m], t[m], dim);

		iter++;
	}

	// Delete allocated memory
	for(int h = 0; h < hs_channels; h++)
	{
		delete[] u[h];
		delete[] ubar[h];
		delete[] nldiv[h];
		delete[] q[h];
		delete[] q_upd[h];
		delete[] q_aux[h];
	}

	for(int m = 0; m < ms_channels; m++)
	{
		delete[] t[m];
		delete[] t_upd[m];
		delete[] t_aux[m];
	}

	delete[] u;
	delete[] ubar;
	delete[] nldiv;
	delete[] q;
	delete[] q_upd;
	delete[] q_aux;
	delete[] t;
	delete[] t_upd;
	delete[] t_aux;
	delete[] aux;
	delete[] aux2;
	delete[] s_aux;
}



// NL regularization with the same weight for all hyperspectral channels.
// Weight structure: wxy[i][n] for each pixel i, and each neighbour n.
// L2 fidelity-term forcing closeness to hyperspectral data on the low-resolution domain, defined by low-pass filtering + subsampling.
// L2 fidelity-term forcing closeness to multispectral data on the high-resolution domain, defined by matrix S.
// L1 data constraint.
void hyperfusion_NL_L1(float **u_upd, float **fH, float **fM, float **S, float **St, float **PH, float **PHint, float **fHint,
		std::vector< std::vector<float> > wxy, std::vector< std::vector<int> > posxy, std::vector< std::vector<float> > wyx,
		std::vector< std::vector<int> > posyx, std::vector< std::vector<int> > posw, float lmbH, float lmbM, float mu,
		float tau, float sigma, float tol, int maxIter, int sampling, float stdBlur, int hs_channels, int ms_channels,
		int width, int height)
{

	// Image size
	int dim = width * height;
	int s_dim = (int) floor((float) dim / (float) (sampling * sampling));


	// Primal variables
	float **u = new float*[hs_channels];
	float **ubar = new float*[hs_channels];

	for(int h = 0; h < hs_channels; h++)
	{
		u[h] = new float[dim];
		ubar[h] = new float[dim];

		libUSTG::fpClear(u_upd[h], 0.0f, dim);
		libUSTG::fpClear(u[h], 0.0f, dim);
		libUSTG::fpClear(ubar[h], 0.0f, dim);
	}


	// Dual variables related to NL regularization
	std::vector< std::vector< std::vector <float> > > p;
	std::vector< std::vector< std::vector <float> > > p_upd;
	std::vector< std::vector< std::vector <float> > > p_aux;

	for(int h = 0; h < hs_channels; h++)
	{
		std::vector< std::vector<float> > haux;

		for(int i = 0; i < dim; i++)
		{
			std::vector<float> aux;
			aux.assign((int) wxy[i].size(), 0.0f);

			haux.push_back(aux);
		}

		p.push_back(haux);
		p_upd.push_back(haux);
		p_aux.push_back(haux);
	}

	float **nldiv = new float*[hs_channels];

	for(int h = 0; h < hs_channels; h++)
	{
		nldiv[h] = new float[dim];
		libUSTG::fpClear(nldiv[h], 0.0f, dim);
	}


	// Dual variables realted to hyperspectral fidelity-term
	float **q = new float*[hs_channels];
	float **q_upd = new float*[hs_channels];
	float **q_aux = new float*[hs_channels];

	for(int h = 0; h < hs_channels; h++)
	{
		q[h] = new float[s_dim];
		q_upd[h] = new float[s_dim];
		q_aux[h] = new float[s_dim];

		libUSTG::fpClear(q[h], 0.0f, s_dim);
		libUSTG::fpClear(q_upd[h], 0.0f, s_dim);
		libUSTG::fpClear(q_aux[h], 0.0f, s_dim);
	}


	// Dual variables realted to multispectral fidelity-term
	float **t = new float*[ms_channels];
	float **t_upd = new float*[ms_channels];
	float **t_aux = new float*[ms_channels];

	for(int m = 0; m < ms_channels; m++)
	{
		t[m] = new float[dim];
		t_upd[m] = new float[dim];
		t_aux[m] = new float[dim];

		libUSTG::fpClear(t[m], 0.0f, dim);
		libUSTG::fpClear(t_upd[m], 0.0f, dim);
		libUSTG::fpClear(t_aux[m], 0.0f, dim);
	}


	// Auxiliar variables
	float *aux = new float[dim];
	float *aux2 = new float[dim];
	float *s_aux = new float[s_dim];

	libUSTG::fpClear(aux, 0.0f, dim);
	libUSTG::fpClear(aux2, 0.0f, dim);
	libUSTG::fpClear(s_aux, 0.0f, s_dim);


	// Primal-dual Chambolle-Pock algorithm
	float error = tol;
	int iter = 0;

	while(iter < maxIter && error >= tol)
	{
		// Compute proximity operator of dualized NL regularization term
		nl_gradient(ubar, p_aux, wxy, posxy, hs_channels, dim);

		for(int h = 0; h < hs_channels; h++)
			for(int i = 0; i < dim; i++)
				for(int w = 0; w < (int) wxy[i].size(); w++)
					p_aux[h][i][w] = p[h][i][w] + sigma * p_aux[h][i][w];

		proxNL(p_upd, p_aux, hs_channels, dim);

		// Compute proximity operator of dualized hyperspectral fidelity term
		for(int h = 0; h < hs_channels; h++)
		{
			libUSTG::fpClear(aux, 0.0f, dim);
			libUSTG::fpClear(s_aux, 0.0f, s_dim);

			FFT_gaussian_convol(aux, ubar[h], stdBlur, width, height);
			downsampling(s_aux, aux, sampling, width, height);

			for(int i = 0; i < s_dim; i++)
				q_aux[h][i] = q[h][i] + sigma * s_aux[i];
		}

		proxDualSquaredL2(q_upd, q_aux, fH, lmbH, sigma, hs_channels, s_dim);

		// Compute proximity operator of dualized multispetral fidelity term
		for(int m = 0; m < ms_channels; m++)
		{
			for(int i = 0; i < dim; i++)
			{
				float Sval = 0.0f;

				for(int h = 0; h < hs_channels; h++)
					Sval += (S[m][h] * ubar[h][i]);

				t_aux[m][i] = t[m][i] + sigma * Sval;
			}
		}

		proxDualSquaredL2(t_upd, t_aux, fM, lmbM, sigma, ms_channels, dim);

		// Compute proximity operator of primal energy term
		nl_divergence(p_upd, nldiv, wxy, wyx, posyx, posw, hs_channels, dim);

		for(int h = 0; h < hs_channels; h++)
		{
			libUSTG::fpClear(aux, 0.0f, dim);
			libUSTG::fpClear(aux2, 0.0f, dim);
			libUSTG::fpClear(s_aux, 0.0f, s_dim);

			upsampling_zeropadding(aux, q_upd[h], sampling, width, height);
			FFT_gaussian_convol(aux, aux, stdBlur, width, height);

			for(int i = 0; i < dim; i++)
			{
				float Stval = 0.0f;

				for(int m = 0; m < ms_channels; m++)
					Stval += (St[h][m] * t_upd[m][i]);

				aux2[i] = Stval;
			}

			for(int i = 0; i < dim; i++)
			{
				float Phintval = PHint[h][i];
				float utilde = u[h][i] + tau * (nldiv[h][i] - aux[i] - aux2[i]);

				if(Phintval > fTiny)
				{
					float arg = Phintval * utilde - PH[h][i] * fHint[h][i];
					float thres = tau * mu * Phintval * Phintval;

					if(arg < - thres)
						//u_upd[h][i] = arg + tau * mu * Phintval;
					u_upd[h][i] = utilde + tau * mu * Phintval;
					else if(arg > thres)
						//u_upd[h][i] = arg - tau * mu * Phintval;
					u_upd[h][i] = utilde - tau * mu * Phintval;
					else
						//u_upd[h][i] = (PH[h][i] * fHint[h][i]) / Phintval;
					u_upd[h][i] =(float) (PH[h][i] * fHint[h][i]) / Phintval;

				} else
				{
					u_upd[h][i] = utilde;
				}
			}
		}

		// Relax primal variable
		for(int h = 0; h < hs_channels; h++)
			for(int i = 0; i < dim; i++)
				ubar[h][i] = 2.0f * u_upd[h][i] - u[h][i];

		// Compute relative error
		error = compute_error(u, u_upd, hs_channels, dim);


		// Update variables
		for(int h = 0; h < hs_channels; h++)
		{
			libUSTG::fpCopy(u_upd[h], u[h], dim);

			for(int i = 0; i < dim; i++)
				p[h][i].assign(p_upd[h][i].begin(), p_upd[h][i].end());
		}

		for(int h = 0; h < hs_channels; h++)
			libUSTG::fpCopy(q_upd[h], q[h], s_dim);

		for(int m = 0; m < ms_channels; m++)
			libUSTG::fpCopy(t_upd[m], t[m], dim);

		iter++;
	}

	// Delete allocated memory
	for(int h = 0; h < hs_channels; h++)
	{
		delete[] u[h];
		delete[] ubar[h];
		delete[] nldiv[h];
		delete[] q[h];
		delete[] q_upd[h];
		delete[] q_aux[h];
	}

	for(int m = 0; m < ms_channels; m++)
	{
		delete[] t[m];
		delete[] t_upd[m];
		delete[] t_aux[m];
	}

	delete[] u;
	delete[] ubar;
	delete[] nldiv;
	delete[] q;
	delete[] q_upd;
	delete[] q_aux;
	delete[] t;
	delete[] t_upd;
	delete[] t_aux;
	delete[] aux;
	delete[] aux2;
	delete[] s_aux;
}



// NL regularization with different weights for each hyperspectral channel.
// Weight structure: wxy[h][i][n] for each channel h, each pixel i, and each neighbour n.
// L2 fidelity-term forcing closeness to hyperspectral data on the low-resolution domain, defined by low-pass filtering + subsampling.
// L2 fidelity-term forcing closeness to multispectral data on the high-resolution domain, defined by matrix S.
// L2 data constraint.
void hyperfusion_NL_wh(float **u_upd, float **fH, float **fM, float **S, float **St, float **PH, float **PHint, float **fHint,
		std::vector< std::vector< std::vector<float> > > wxy, std::vector< std::vector< std::vector<int> > > posxy,
		std::vector< std::vector< std::vector<float> > > wyx, std::vector< std::vector< std::vector<int> > > posyx,
		std::vector< std::vector< std::vector<int> > > posw, float lmbH, float lmbM, float mu, float tau, float sigma,
		float tol, int maxIter, int sampling, float stdBlur, int hs_channels, int ms_channels, int width, int height)
{

	// Image size
	int dim = width * height;
	int s_dim = (int) floor((float) dim / (float) (sampling * sampling));


	// Primal variables
	float **u = new float*[hs_channels];
	float **ubar = new float*[hs_channels];

	for(int h = 0; h < hs_channels; h++)
	{
		u[h] = new float[dim];
		ubar[h] = new float[dim];

		libUSTG::fpClear(u_upd[h], 0.0f, dim);
		libUSTG::fpClear(u[h], 0.0f, dim);
		libUSTG::fpClear(ubar[h], 0.0f, dim);
	}


	// Dual variables related to NL regularization
	std::vector< std::vector< std::vector <float> > > p;
	std::vector< std::vector< std::vector <float> > > p_upd;
	std::vector< std::vector< std::vector <float> > > p_aux;

	for(int h = 0; h < hs_channels; h++)
	{
		std::vector< std::vector<float> > haux;

		for(int i = 0; i < dim; i++)
		{
			std::vector<float> aux;
			aux.assign((int) wxy[h][i].size(), 0.0f);

			haux.push_back(aux);
		}

		p.push_back(haux);
		p_upd.push_back(haux);
		p_aux.push_back(haux);
	}

	float **nldiv = new float*[hs_channels];

	for(int h = 0; h < hs_channels; h++)
	{
		nldiv[h] = new float[dim];
		libUSTG::fpClear(nldiv[h], 0.0f, dim);
	}


	// Dual variables realted to hyperspectral fidelity-term
	float **q = new float*[hs_channels];
	float **q_upd = new float*[hs_channels];
	float **q_aux = new float*[hs_channels];

	for(int h = 0; h < hs_channels; h++)
	{
		q[h] = new float[s_dim];
		q_upd[h] = new float[s_dim];
		q_aux[h] = new float[s_dim];

		libUSTG::fpClear(q[h], 0.0f, s_dim);
		libUSTG::fpClear(q_upd[h], 0.0f, s_dim);
		libUSTG::fpClear(q_aux[h], 0.0f, s_dim);
	}


	// Dual variables realted to multispectral fidelity-term
	float **t = new float*[ms_channels];
	float **t_upd = new float*[ms_channels];
	float **t_aux = new float*[ms_channels];

	for(int m = 0; m < ms_channels; m++)
	{
		t[m] = new float[dim];
		t_upd[m] = new float[dim];
		t_aux[m] = new float[dim];

		libUSTG::fpClear(t[m], 0.0f, dim);
		libUSTG::fpClear(t_upd[m], 0.0f, dim);
		libUSTG::fpClear(t_aux[m], 0.0f, dim);
	}


	// Auxiliar variables
	float *aux = new float[dim];
	float *aux2 = new float[dim];
	float *s_aux = new float[s_dim];

	libUSTG::fpClear(aux, 0.0f, dim);
	libUSTG::fpClear(aux2, 0.0f, dim);
	libUSTG::fpClear(s_aux, 0.0f, s_dim);


	// Primal-dual Chambolle-Pock algorithm
	float error = tol;
	int iter = 0;

	while(iter < maxIter && error >= tol)
	{
		// Compute proximity operator of dualized NL regularization term
		nl_gradient(ubar, p_aux, wxy, posxy, hs_channels, dim);

		for(int h = 0; h < hs_channels; h++)
			for(int i = 0; i < dim; i++)
				for(int w = 0; w < (int) wxy[h][i].size(); w++)
					p_aux[h][i][w] = p[h][i][w] + sigma * p_aux[h][i][w];

		proxNL(p_upd, p_aux, hs_channels, dim);

		// Compute proximity operator of dualized hyperspectral fidelity term
		for(int h = 0; h < hs_channels; h++)
		{
			libUSTG::fpClear(aux, 0.0f, dim);
			libUSTG::fpClear(s_aux, 0.0f, s_dim);

			FFT_gaussian_convol(aux, ubar[h], stdBlur, width, height);
			downsampling(s_aux, aux, sampling, width, height);

			for(int i = 0; i < s_dim; i++)
				q_aux[h][i] = q[h][i] + sigma * s_aux[i];
		}

		proxDualSquaredL2(q_upd, q_aux, fH, lmbH, sigma, hs_channels, s_dim);

		// Compute proximity operator of dualized multispetral fidelity term
		for(int m = 0; m < ms_channels; m++)
		{
			for(int i = 0; i < dim; i++)
			{
				float Sval = 0.0f;

				for(int h = 0; h < hs_channels; h++)
					Sval += (S[m][h] * ubar[h][i]);

				t_aux[m][i] = t[m][i] + sigma * Sval;
			}
		}

		proxDualSquaredL2(t_upd, t_aux, fM, lmbM, sigma, ms_channels, dim);

		// Compute proximity operator of primal energy term
		nl_divergence(p_upd, nldiv, wxy, wyx, posyx, posw, hs_channels, dim);

		for(int h = 0; h < hs_channels; h++)
		{
			libUSTG::fpClear(aux, 0.0f, dim);
			libUSTG::fpClear(aux2, 0.0f, dim);
			libUSTG::fpClear(s_aux, 0.0f, s_dim);

			upsampling_zeropadding(aux, q_upd[h], sampling, width, height);
			FFT_gaussian_convol(aux, aux, stdBlur, width, height);

			for(int i = 0; i < dim; i++)
			{
				float Stval = 0.0f;

				for(int m = 0; m < ms_channels; m++)
					Stval += (St[h][m] * t_upd[m][i]);

				aux2[i] = Stval;
			}

			//La partie correspondante ?? la norme L2
			for(int i = 0; i < dim; i++)
			{
				float phval = PHint[h][i];
				u_upd[h][i] = (u[h][i] + tau * (nldiv[h][i] - aux[i] - aux2[i] + mu * phval * PH[h][i] * fHint[h][i])) / (1.0f + tau * mu * phval * phval);
			}
		}

		// Relax primal variable
		for(int h = 0; h < hs_channels; h++)
			for(int i = 0; i < dim; i++)
				ubar[h][i] = 2.0f * u_upd[h][i] - u[h][i];

		// Compute relative error
		error = compute_error(u, u_upd, hs_channels, dim);

		//printf("iter = %i; error = %.10f \n", iter, error);

		// Update variables
		for(int h = 0; h < hs_channels; h++)
		{
			libUSTG::fpCopy(u_upd[h], u[h], dim);

			for(int i = 0; i < dim; i++)
				p[h][i].assign(p_upd[h][i].begin(), p_upd[h][i].end());
		}

		for(int h = 0; h < hs_channels; h++)
			libUSTG::fpCopy(q_upd[h], q[h], s_dim);

		for(int m = 0; m < ms_channels; m++)
			libUSTG::fpCopy(t_upd[m], t[m], dim);

		iter++;
	}

	// Delete allocated memory
	for(int h = 0; h < hs_channels; h++)
	{
		delete[] u[h];
		delete[] ubar[h];
		delete[] nldiv[h];
		delete[] q[h];
		delete[] q_upd[h];
		delete[] q_aux[h];
	}

	for(int m = 0; m < ms_channels; m++)
	{
		delete[] t[m];
		delete[] t_upd[m];
		delete[] t_aux[m];
	}

	delete[] u;
	delete[] ubar;
	delete[] nldiv;
	delete[] q;
	delete[] q_upd;
	delete[] q_aux;
	delete[] t;
	delete[] t_upd;
	delete[] t_aux;
	delete[] aux;
	delete[] aux2;
	delete[] s_aux;
}



// NL regularization with the same weight for all hyperspectral channels.
// Weight structure: wxy[i][n] for each pixel i, and each neighbour n.
// L2 fidelity-term forcing closeness to hyperspectral data on the low-resolution domain, defined by low-pass filtering + subsampling.
// L2 fidelity-term forcing closeness to multispectral data on the high-resolution domain, defined by matrix S.
// L2 data constraint.
void hyperfusion_NL(float **u_upd, float **fH, float **fM, float **S, float **St, float **PH, float **PHint, float **fHint,
		std::vector< std::vector<float> > wxy, std::vector< std::vector<int> > posxy, std::vector< std::vector<float> > wyx,
		std::vector< std::vector<int> > posyx, std::vector< std::vector<int> > posw, float lmbH, float lmbM, float mu,
		float tau, float sigma, float tol, int maxIter, int sampling, float stdBlur, int hs_channels, int ms_channels,
		int width, int height)
{

	// Image size
	int dim = width * height;
	int s_dim = (int) floor((float) dim / (float) (sampling * sampling));


	// Primal variables
	float **u = new float*[hs_channels];
	float **ubar = new float*[hs_channels];

	for(int h = 0; h < hs_channels; h++)
	{
		u[h] = new float[dim];
		ubar[h] = new float[dim];

		libUSTG::fpClear(u_upd[h], 0.0f, dim);
		libUSTG::fpClear(u[h], 0.0f, dim);
		libUSTG::fpClear(ubar[h], 0.0f, dim);
	}


	// Dual variables related to NL regularization
	std::vector< std::vector< std::vector <float> > > p;
	std::vector< std::vector< std::vector <float> > > p_upd;
	std::vector< std::vector< std::vector <float> > > p_aux;

	for(int h = 0; h < hs_channels; h++)
	{
		std::vector< std::vector<float> > haux;

		for(int i = 0; i < dim; i++)
		{
			std::vector<float> aux;
			aux.assign((int) wxy[i].size(), 0.0f);

			haux.push_back(aux);
		}

		p.push_back(haux);
		p_upd.push_back(haux);
		p_aux.push_back(haux);
	}

	float **nldiv = new float*[hs_channels];

	for(int h = 0; h < hs_channels; h++)
	{
		nldiv[h] = new float[dim];
		libUSTG::fpClear(nldiv[h], 0.0f, dim);
	}


	// Dual variables realted to hyperspectral fidelity-term
	float **q = new float*[hs_channels];
	float **q_upd = new float*[hs_channels];
	float **q_aux = new float*[hs_channels];

	for(int h = 0; h < hs_channels; h++)
	{
		q[h] = new float[s_dim];
		q_upd[h] = new float[s_dim];
		q_aux[h] = new float[s_dim];

		libUSTG::fpClear(q[h], 0.0f, s_dim);
		libUSTG::fpClear(q_upd[h], 0.0f, s_dim);
		libUSTG::fpClear(q_aux[h], 0.0f, s_dim);
	}


	// Dual variables realted to multispectral fidelity-term
	float **t = new float*[ms_channels];
	float **t_upd = new float*[ms_channels];
	float **t_aux = new float*[ms_channels];

	for(int m = 0; m < ms_channels; m++)
	{
		t[m] = new float[dim];
		t_upd[m] = new float[dim];
		t_aux[m] = new float[dim];

		libUSTG::fpClear(t[m], 0.0f, dim);
		libUSTG::fpClear(t_upd[m], 0.0f, dim);
		libUSTG::fpClear(t_aux[m], 0.0f, dim);
	}


	// Auxiliar variables
	float *aux = new float[dim];
	float *aux2 = new float[dim];
	float *s_aux = new float[s_dim];

	libUSTG::fpClear(aux, 0.0f, dim);
	libUSTG::fpClear(aux2, 0.0f, dim);
	libUSTG::fpClear(s_aux, 0.0f, s_dim);


	// Primal-dual Chambolle-Pock algorithm
	float error = tol;
	int iter = 0;

	while(iter < maxIter && error >= tol)
	{
		// Compute proximity operator of dualized NL regularization term
		nl_gradient(ubar, p_aux, wxy, posxy, hs_channels, dim);

		for(int h = 0; h < hs_channels; h++)
			for(int i = 0; i < dim; i++)
				for(int w = 0; w < (int) wxy[i].size(); w++)
					p_aux[h][i][w] = p[h][i][w] + sigma * p_aux[h][i][w];

		proxNL(p_upd, p_aux, hs_channels, dim);

		// Compute proximity operator of dualized hyperspectral fidelity term
		for(int h = 0; h < hs_channels; h++)
		{
			libUSTG::fpClear(aux, 0.0f, dim);
			libUSTG::fpClear(s_aux, 0.0f, s_dim);

			FFT_gaussian_convol(aux, ubar[h], stdBlur, width, height);
			downsampling(s_aux, aux, sampling, width, height);

			for(int i = 0; i < s_dim; i++)
				q_aux[h][i] = q[h][i] + sigma * s_aux[i];
		}

		proxDualSquaredL2(q_upd, q_aux, fH, lmbH, sigma, hs_channels, s_dim);

		// Compute proximity operator of dualized multispetral fidelity term
		for(int m = 0; m < ms_channels; m++)
		{
			for(int i = 0; i < dim; i++)
			{
				float Sval = 0.0f;

				for(int h = 0; h < hs_channels; h++)
					Sval += (S[m][h] * ubar[h][i]);

				t_aux[m][i] = t[m][i] + sigma * Sval;
			}
		}

		proxDualSquaredL2(t_upd, t_aux, fM, lmbM, sigma, ms_channels, dim);

		// Compute proximity operator of primal energy term
		nl_divergence(p_upd, nldiv, wxy, wyx, posyx, posw, hs_channels, dim);

		for(int h = 0; h < hs_channels; h++)
		{
			libUSTG::fpClear(aux, 0.0f, dim);
			libUSTG::fpClear(aux2, 0.0f, dim);
			libUSTG::fpClear(s_aux, 0.0f, s_dim);

			upsampling_zeropadding(aux, q_upd[h], sampling, width, height);
			FFT_gaussian_convol(aux, aux, stdBlur, width, height);

			for(int i = 0; i < dim; i++)
			{
				float Stval = 0.0f;

				for(int m = 0; m < ms_channels; m++)
					Stval += (St[h][m] * t_upd[m][i]);

				aux2[i] = Stval;
			}

			for(int i = 0; i < dim; i++)
			{
				float phval = PHint[h][i];
				u_upd[h][i] = (u[h][i] + tau * (nldiv[h][i] - aux[i] - aux2[i] + mu * phval * PH[h][i] * fHint[h][i])) / (1.0f + tau * mu * phval * phval);
			}
		}

		// Relax primal variable
		for(int h = 0; h < hs_channels; h++)
			for(int i = 0; i < dim; i++)
				ubar[h][i] = 2.0f * u_upd[h][i] - u[h][i];

		// Compute relative error
		error = compute_error(u, u_upd, hs_channels, dim);


		// Update variables
		for(int h = 0; h < hs_channels; h++)
		{
			libUSTG::fpCopy(u_upd[h], u[h], dim);

			for(int i = 0; i < dim; i++)
				p[h][i].assign(p_upd[h][i].begin(), p_upd[h][i].end());
		}

		for(int h = 0; h < hs_channels; h++)
			libUSTG::fpCopy(q_upd[h], q[h], s_dim);

		for(int m = 0; m < ms_channels; m++)
			libUSTG::fpCopy(t_upd[m], t[m], dim);

		iter++;
	}

	// Delete allocated memory
	for(int h = 0; h < hs_channels; h++)
	{
		delete[] u[h];
		delete[] ubar[h];
		delete[] nldiv[h];
		delete[] q[h];
		delete[] q_upd[h];
		delete[] q_aux[h];
	}

	for(int m = 0; m < ms_channels; m++)
	{
		delete[] t[m];
		delete[] t_upd[m];
		delete[] t_aux[m];
	}

	delete[] u;
	delete[] ubar;
	delete[] nldiv;
	delete[] q;
	delete[] q_upd;
	delete[] q_aux;
	delete[] t;
	delete[] t_upd;
	delete[] t_aux;
	delete[] aux;
	delete[] aux2;
	delete[] s_aux;
}



// Compute proximity operator of dualized squared L2 data term
void proxDualSquaredL2(float **p_upd, float **p_arg, float **f, float lmb, float sigma, int num_channels, int dim)
{
	float coeff = lmb / (sigma + lmb);

	for(int c = 0; c < num_channels; c++)
		for(int i = 0; i < dim; i++)
			p_upd[c][i] = coeff * (p_arg[c][i] - sigma * f[c][i]);
}



// Compute proximity operator of NL regularization.
void proxNL(std::vector< std::vector< std::vector <float> > > &p_upd, std::vector< std::vector< std::vector <float> > > &p_arg,
		int num_channels, int dim)
{
	for(int c = 0; c < num_channels; c++)
	{
		for(int i = 0; i < dim; i++)
		{
			float norm = 0.0f;

			for(int w = 0; w < (int) p_arg[c][i].size(); w++)
			{
				float p = p_arg[c][i][w];
				norm += (p * p);
			}

			norm = sqrt(norm);
			float maxval = MAX(1.0f, norm);

			for(int w = 0; w < (int) p_arg[c][i].size(); w++)
				p_upd[c][i][w] = p_arg[c][i][w] / maxval;
		}
	}
}



// For each hyperspectral channel, compute the nonlocal gradient.
// The weights are different for each hyperspectral channel.
void nl_gradient(float **data, std::vector< std::vector< std::vector<float> > > &nlgrad,
		std::vector< std::vector< std::vector<float> > > &wxy, std::vector< std::vector< std::vector<int> > > &posxy,
		int num_channels, int dim)
{
	for(int c = 0; c < num_channels; c++)
	{
		for(int l = 0; l < dim; l++)
		{
			for(int w = 0; w < (int) wxy[c][l].size(); w++)
			{
				// Index of neighbouring pixel with non-zero weight
				int l0 = posxy[c][l][w];

				// Weight
				float weight = wxy[c][l][w];

				//std::cout<<" c= "<<c<<" l= "<<l<<" w= "<< w <<" l0= "<< l0 <<" weight "<< weight <<std::endl;

				// Compute gradient at (l, l0)
				nlgrad[c][l][w] = (data[c][l0] - data[c][l]) * weight;
			}
		}
	}
}



// For each hyperspectral channel, compute the nonlocal gradient.
// The weights are the same for all hyperspectral channels.
void nl_gradient(float **data, std::vector< std::vector< std::vector<float> > > &nlgrad, std::vector< std::vector<float> > &wxy,
		std::vector< std::vector<int> > &posxy, int num_channels, int dim)
{

	for(int l = 0; l < dim; l++)
	{
		for(int w = 0; w < (int) wxy[l].size(); w++)
		{
			// Index of neighbouring pixel with non-zero weight
			int l0 = posxy[l][w];

			// Weight
			float weight = wxy[l][w];

			// Compute gradient at (l, l0)
			for(int c = 0; c < num_channels; c++)
				nlgrad[c][l][w] = (data[c][l0] - data[c][l]) * weight;
		}
	}
}



// For each hyperspectral channel, compute the nonlocal divergence as div_w = -nabla_w^T.
// The weights are different for each hyperspectral channel.
void nl_divergence(std::vector< std::vector< std::vector<float> > > &data, float **nldiv,
		std::vector< std::vector< std::vector<float> > > &wxy, std::vector< std::vector< std::vector<float> > > &wyx,
		std::vector< std::vector< std::vector<int> > > &posyx, std::vector< std::vector< std::vector<int> > > &posw,
		int num_channels, int dim)
{
	/*int l0_last;
    	int w0_last;
    	int weight_last;
    	float data_last;
    	int c_last;
    	int w_last;*/

	for(int c = 0; c < num_channels; c++)
	{
		//c_last=c;
		for(int l = 0; l < dim; l++)
		{
			// Auxiliar variable
			float divl = 0.0f;

			// Terms involving w(x,y)
			for(int w = 0; w < (int) wxy[c][l].size(); w++)
			{
				// Weight
				float weight = wxy[c][l][w];

				// Assign value
				divl += data[c][l][w] * weight;
			}

			// Terms involving w(y,x)
			for(int w = 0; w < (int) wyx[c][l].size(); w++)
			{
				//w_last=w;
				// Index of neighbouring pixel
				int l0 = posyx[c][l][w];
				//l0_last=l0;

				// Index of w(y,x)
				int w0 = posw[c][l][w];
				//w0_last=w0;

				// Weight
				float weight = wyx[c][l][w];
				//weight_last=weight;

				// Assign value
				divl -= data[c][l0][w0] * weight;

				//data_last=data[c][l0][w0];
			}

			// Save divergence at current pixel
			nldiv[c][l] = divl;
		}
		/*std::cout<<" w is "<< w_last <<" c is "<< c_last <<"l0 is "<< l0_last<<" w0 is "<< w0_last <<" weight is "<< weight_last <<std::endl;
            std::cout<<"data is "<<data_last<<std::endl;
            printf("divergence: out of the inner loop \n");*/
	}

	//printf("Out of the divergence \n");
}



// For each hyperspectral channel, compute the nonlocal divergence as div_w = -nabla_w^T.
// The weights are the same for all hyperspectral channels.
void nl_divergence(std::vector< std::vector< std::vector<float> > > &data, float **nldiv, std::vector< std::vector<float> > &wxy,
		std::vector< std::vector<float> > &wyx, std::vector< std::vector<int> > &posyx,
		std::vector< std::vector<int> > &posw, int num_channels, int dim)
{
	for(int c = 0; c < num_channels; c++)
	{
		for(int l = 0; l < dim; l++)
		{
			// Auxiliar variable
			float divl = 0.0f;

			// Terms involving w(x,y)
			for(int w = 0; w < (int) wxy[l].size(); w++)
			{
				// Weight
				float weight = wxy[l][w];

				// Assign value
				divl += data[c][l][w] * weight;
			}

			// Terms involving w(y,x)
			for(int w = 0; w < (int) wyx[l].size(); w++)
			{
				// Index of neighbouring pixel
				int l0 = posyx[l][w];

				// Index of w(y,x)
				int w0 = posw[l][w];

				// Weight
				float weight = wyx[l][w];

				// Assign value
				divl -= data[c][l0][w0] * weight;
			}

			// Save divergence at current pixel
			nldiv[c][l] = divl;
		}
	}
}



// Compute Gaussian convolution in Fourier domain
void FFT_gaussian_convol(float *convolved, float *data, float stdBlur, int width, int height)
{
	// Image size
	int dim = width * height;

	// Double variables
	double d_std = (double) stdBlur;
	double *d_data = new double[dim];
	double *d_convolved = new double[dim];

	for(int i = 0; i < dim; i++)
		d_data[i] = (double) data[i];

	// FFT variables
	fftw_plan p;
	double *data_fft = (double*) fftw_malloc(dim * sizeof(double));
	double *convolved_fft = (double*) fftw_malloc(dim * sizeof(double));

	// Gaussian kernel
	int dim4 = 4 * dim;
	double scale = (d_std * d_std) / 2e0;
	double normx = M_PI / (double) width;
	normx *= normx;
	double normy = M_PI / (double) height;
	normy *= normy;

	// Compute the FFT of data
	p = fftw_plan_r2r_2d(height, width, d_data, data_fft, FFTW_REDFT10, FFTW_REDFT10, FFTW_ESTIMATE);
	fftw_execute(p);
	fftw_destroy_plan(p);

	// Compute Gaussian convolution in Fourier domain
	for(int j = 0; j < height; j++)
	{
		int l = j * width;

		for(int i = 0; i < width; i++)
		{
			double kernel = exp((double)(-scale) * (normx * i * i + normy * j * j));
			convolved_fft[l+i] = (double) (data_fft[l+i] * kernel);
		}
	}

	// Compute the inverse FFT of out_fft
	p = fftw_plan_r2r_2d(height, width, convolved_fft, d_convolved, FFTW_REDFT01, FFTW_REDFT01, FFTW_ESTIMATE);
	fftw_execute(p);
	fftw_destroy_plan(p);

	for(int i = 0; i < dim; i++)
		d_convolved[i] /= (double) dim4;

	// Compute float solution
	for(int i = 0; i < dim; i++)
		convolved[i] = (float) d_convolved[i];

	// Delete alocated memory
	delete[] d_data;
	delete[] d_convolved;

	fftw_free(data_fft);
	fftw_free(convolved_fft);
}



// Downsampling
void downsampling(float *downsampled, float *data, int sampling_factor, int width, int height)
{
	int s_width = (int) floor((float) width / (float) sampling_factor);
	int s_height = (int) floor((float) height / (float) sampling_factor);

	for(int j = 0; j < s_height; j++)
		for(int i = 0; i < s_width; i++)
			downsampled[j * s_width + i] = data[j * sampling_factor * width + i * sampling_factor];
}



// Upsampling by zero-padding
void upsampling_zeropadding(float *upsampled, float *data, int sampling_factor, int width, int height)
{
	libUSTG::fpClear(upsampled, 0.0f, width * height);

	int s_width = (int) floor((float) width / (float) sampling_factor);

	for(int j = 0; j < height; j++)
	{
		int s_j = (int) floor((float) j / (float) sampling_factor);

		for(int i = 0; i < width; i++)
		{
			int s_i = (int) floor((float) i / (float) sampling_factor);

			if((i % sampling_factor == 0) && (j % sampling_factor == 0))
				upsampled[j * width + i] = data[s_j * s_width + s_i];
		}
	}
}



// Compute L2 error between two consecutive iterations
float compute_error(float **u, float **u_upd, int num_channels, int dim)
{
	float error = 0.0f;

	for(int n = 0; n < num_channels; n++)
	{
		for(int i = 0; i < dim; i++)
		{
			float value = u_upd[n][i] - u[n][i];
			error += value * value;
		}
	}

	error /= (float) (num_channels * dim);
	error = sqrtf(error);

	return error;
}




} // libUSTGHYPERSPECTRAL
